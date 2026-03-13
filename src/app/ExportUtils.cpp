#include "app/ExportUtils.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include "app/Resource.h"

namespace radiary {

std::uint8_t FloatToByte(const float value) {
    const long long scaled = std::llround(static_cast<double>(value) * 255.0);
    return static_cast<std::uint8_t>(std::clamp(scaled, 0ll, 255ll));
}

float HalfToFloat(const std::uint16_t value) {
    const std::uint32_t sign = (static_cast<std::uint32_t>(value & 0x8000u)) << 16u;
    const std::uint32_t exponent = (value >> 10u) & 0x1Fu;
    const std::uint32_t mantissa = value & 0x03FFu;

    std::uint32_t bits = 0u;
    if (exponent == 0u) {
        if (mantissa == 0u) {
            bits = sign;
        } else {
            std::uint32_t normalizedMantissa = mantissa;
            std::uint32_t normalizedExponent = 113u;
            while ((normalizedMantissa & 0x0400u) == 0u) {
                normalizedMantissa <<= 1u;
                --normalizedExponent;
            }
            normalizedMantissa &= 0x03FFu;
            bits = sign | (normalizedExponent << 23u) | (normalizedMantissa << 13u);
        }
    } else if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13u);
    } else {
        bits = sign | ((exponent + 112u) << 23u) | (mantissa << 13u);
    }

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::uint32_t PackBgra(const std::uint8_t r, const std::uint8_t g, const std::uint8_t b, const std::uint8_t a) {
    return static_cast<std::uint32_t>(b)
        | (static_cast<std::uint32_t>(g) << 8U)
        | (static_cast<std::uint32_t>(r) << 16U)
        | (static_cast<std::uint32_t>(a) << 24U);
}

void CompositePixelsOver(std::vector<std::uint32_t>& destination, const std::vector<std::uint32_t>& source) {
    if (destination.size() != source.size()) {
        return;
    }
    for (std::size_t index = 0; index < destination.size(); ++index) {
        const std::uint32_t src = source[index];
        const std::uint32_t dst = destination[index];
        const std::uint32_t srcAlpha = (src >> 24U) & 0xFFU;
        if (srcAlpha == 0u) {
            continue;
        }
        if (srcAlpha == 255u) {
            destination[index] = src;
            continue;
        }

        const std::uint32_t invAlpha = 255u - srcAlpha;
        const std::uint32_t srcR = (src >> 16U) & 0xFFU;
        const std::uint32_t srcG = (src >> 8U) & 0xFFU;
        const std::uint32_t srcB = src & 0xFFU;
        const std::uint32_t srcA = (src >> 24U) & 0xFFU;
        const std::uint32_t dstR = (dst >> 16U) & 0xFFU;
        const std::uint32_t dstG = (dst >> 8U) & 0xFFU;
        const std::uint32_t dstB = dst & 0xFFU;
        const std::uint32_t dstA = (dst >> 24U) & 0xFFU;
        const std::uint8_t outR = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, srcR + ((dstR * invAlpha + 127u) / 255u)));
        const std::uint8_t outG = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, srcG + ((dstG * invAlpha + 127u) / 255u)));
        const std::uint8_t outB = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, srcB + ((dstB * invAlpha + 127u) / 255u)));
        const std::uint8_t outA = static_cast<std::uint8_t>(std::min<std::uint32_t>(255u, srcA + ((dstA * invAlpha + 127u) / 255u)));
        destination[index] = PackBgra(outR, outG, outB, outA);
    }
}

bool SavePixelsToImageFile(
    const std::filesystem::path& path,
    const std::vector<std::uint32_t>& pixels,
    const int width,
    const int height,
    const bool jpeg) {
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IPropertyBag2* properties = nullptr;
    bool success = false;
    const GUID containerFormat = jpeg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng;
    std::vector<BYTE> exportBytes;
    WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
    UINT stride = static_cast<UINT>(width * 4);
    UINT bufferSize = static_cast<UINT>(width * height * 4);
    BYTE* pixelData = reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(pixels.data()));
    if (jpeg) {
        pixelFormat = GUID_WICPixelFormat24bppBGR;
        stride = static_cast<UINT>(width * 3);
        bufferSize = static_cast<UINT>(width * height * 3);
        exportBytes.resize(static_cast<std::size_t>(bufferSize));
        for (int index = 0; index < width * height; ++index) {
            const std::uint32_t pixel = pixels[static_cast<std::size_t>(index)];
            exportBytes[static_cast<std::size_t>(index) * 3 + 0] = static_cast<BYTE>(pixel & 0xFFU);
            exportBytes[static_cast<std::size_t>(index) * 3 + 1] = static_cast<BYTE>((pixel >> 8U) & 0xFFU);
            exportBytes[static_cast<std::size_t>(index) * 3 + 2] = static_cast<BYTE>((pixel >> 16U) & 0xFFU);
        }
        pixelData = exportBytes.data();
    }

    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        && SUCCEEDED(factory->CreateStream(&stream))
        && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))
        && SUCCEEDED(factory->CreateEncoder(containerFormat, nullptr, &encoder))
        && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, &properties))
        && SUCCEEDED(frame->Initialize(properties))
        && SUCCEEDED(frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) {
        if (SUCCEEDED(frame->SetPixelFormat(&pixelFormat))
            && SUCCEEDED(frame->WritePixels(static_cast<UINT>(height), stride, bufferSize, pixelData))
            && SUCCEEDED(frame->Commit())
            && SUCCEEDED(encoder->Commit())) {
            success = true;
        }
    }

    if (properties) { properties->Release(); }
    if (frame) { frame->Release(); }
    if (encoder) { encoder->Release(); }
    if (stream) { stream->Release(); }
    if (factory) { factory->Release(); }
    return success;
}

namespace {

std::filesystem::path GetExecutablePath() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2, L'\0');
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length == 0) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
}

std::filesystem::path GetExecutableDirectory() {
    const std::filesystem::path executablePath = GetExecutablePath();
    if (executablePath.empty()) {
        return {};
    }
    return executablePath.parent_path();
}

std::filesystem::path GetEmbeddedFfmpegCachePath(const DWORD resourceSize) {
    std::error_code pathError;
    std::filesystem::path cacheRoot = std::filesystem::temp_directory_path(pathError);
    if (pathError || cacheRoot.empty()) {
        cacheRoot = GetExecutableDirectory();
    }
    if (cacheRoot.empty()) {
        return {};
    }

    std::uint64_t executableStamp = 0;
    const std::filesystem::path executablePath = GetExecutablePath();
    if (!executablePath.empty()) {
        const auto writeTime = std::filesystem::last_write_time(executablePath, pathError);
        if (!pathError) {
            executableStamp = static_cast<std::uint64_t>(writeTime.time_since_epoch().count());
        }
    }

    return cacheRoot
        / L"Radiary"
        / L"ffmpeg"
        / (L"ffmpeg-" + std::to_wstring(executableStamp) + L"-" + std::to_wstring(resourceSize) + L".exe");
}

bool WriteBinaryFile(const std::filesystem::path& path, const void* data, const std::size_t size) {
    if (path.empty() || data == nullptr || size == 0) {
        return false;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        return false;
    }

    const std::filesystem::path tempPath = path.parent_path() / (path.filename().wstring() + L".tmp");
    {
        std::ofstream stream(tempPath, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!stream) {
            return false;
        }
    }

    std::error_code replaceError;
    std::filesystem::remove(path, replaceError);
    replaceError.clear();
    std::filesystem::rename(tempPath, path, replaceError);
    if (replaceError) {
        std::filesystem::remove(tempPath, replaceError);
        return false;
    }
    return true;
}

std::filesystem::path ExtractEmbeddedFfmpegPath() {
    HMODULE module = GetModuleHandleW(nullptr);
    if (module == nullptr) {
        return {};
    }

    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_RADIARY_FFMPEG), RT_RCDATA);
    if (resource == nullptr) {
        return {};
    }

    const DWORD resourceSize = SizeofResource(module, resource);
    if (resourceSize == 0u) {
        return {};
    }

    HGLOBAL loadedResource = LoadResource(module, resource);
    if (loadedResource == nullptr) {
        return {};
    }

    const void* resourceBytes = LockResource(loadedResource);
    if (resourceBytes == nullptr) {
        return {};
    }

    const std::filesystem::path extractedPath = GetEmbeddedFfmpegCachePath(resourceSize);
    if (extractedPath.empty()) {
        return {};
    }

    std::error_code fileError;
    const bool alreadyExtracted = std::filesystem::exists(extractedPath, fileError)
        && !fileError
        && std::filesystem::is_regular_file(extractedPath, fileError)
        && !fileError
        && std::filesystem::file_size(extractedPath, fileError) == resourceSize;
    if (alreadyExtracted) {
        return extractedPath;
    }

    if (!WriteBinaryFile(extractedPath, resourceBytes, static_cast<std::size_t>(resourceSize))) {
        return {};
    }
    return extractedPath;
}

std::filesystem::path FindExternalBundledFfmpegPath() {
    const std::filesystem::path executableDirectory = GetExecutableDirectory();
    if (executableDirectory.empty()) {
        return {};
    }

    const std::array<std::filesystem::path, 3> candidates {{
        executableDirectory / L"ffmpeg.exe",
        executableDirectory / L"tools" / L"ffmpeg.exe",
        executableDirectory / L"third_party" / L"ffmpeg" / L"bin" / L"ffmpeg.exe"
    }};
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code existsError;
        if (std::filesystem::exists(candidate, existsError)) {
            return candidate;
        }
    }
    return {};
}

}  // namespace

std::filesystem::path FindBundledFfmpegPath() {
    const std::filesystem::path embeddedPath = ExtractEmbeddedFfmpegPath();
    if (!embeddedPath.empty()) {
        return embeddedPath;
    }
    return FindExternalBundledFfmpegPath();
}

std::wstring Utf8FileTextToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring ReadTextFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return Utf8FileTextToWide(buffer.str());
}

void ApplyDarkTitleBar(void* window) {
    HWND hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr) {
        return;
    }

    const BOOL useDarkMode = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode));

    const BOOL useHostBackdropBrush = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_HOSTBACKDROPBRUSH, &useHostBackdropBrush, sizeof(useHostBackdropBrush));

    const DWM_SYSTEMBACKDROP_TYPE backdropType = DWMSBT_MAINWINDOW;
    const bool backdropApplied = SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType)));

    const DWORD captionColor = backdropApplied ? DWMWA_COLOR_DEFAULT : RGB(36, 36, 40);
    const COLORREF textColor = RGB(240, 240, 245);
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textColor, sizeof(textColor));
}

}  // namespace radiary
