#include "renderer/D3D11ShaderUtils.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace radiary {

namespace {

std::filesystem::path ResolveShaderPath(const wchar_t* shaderFilename) {
    wchar_t modulePath[MAX_PATH] {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path executablePath;
    if (length > 0) {
        executablePath = std::filesystem::path(modulePath).parent_path();
    }

#ifdef RADIARY_SHADER_DIR
    const std::filesystem::path shaderDirectory = RADIARY_SHADER_DIR;
#else
    const std::filesystem::path shaderDirectory = L"shaders";
#endif

    return executablePath / shaderDirectory / shaderFilename;
}

std::wstring AsciiToWide(const char* value) {
    std::wstring wide;
    if (value == nullptr) {
        return wide;
    }
    while (*value != '\0') {
        wide.push_back(static_cast<wchar_t>(*value));
        ++value;
    }
    return wide;
}

std::wstring MakePrecompiledShaderFilename(
    const wchar_t* shaderFilename,
    const char* entryPoint,
    const char* target) {
    const std::filesystem::path shaderPath(shaderFilename);
    std::wstring filename = shaderPath.stem().wstring();
    filename += L"_";
    filename += AsciiToWide(entryPoint);
    filename += L"_";
    filename += AsciiToWide(target);
    filename += L".cso";
    return filename;
}

bool ShouldForceRuntimeShaderCompilation() {
    char value[8] {};
    const DWORD length = GetEnvironmentVariableA("RADIARY_FORCE_RUNTIME_SHADER_COMPILE", value, static_cast<DWORD>(std::size(value)));
    return length > 0 && value[0] != '\0' && value[0] != '0';
}

}  // namespace

UINT GetOptimizedD3D11ShaderCompileFlags(const bool enableStrictness, const bool preferFlowControl) {
    UINT compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (enableStrictness) {
        compileFlags |= D3DCOMPILE_ENABLE_STRICTNESS;
    }
    if (preferFlowControl) {
        compileFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
    }
    return compileFlags;
}

UINT GetDevelopmentD3D11ShaderCompileFlags(const bool enableStrictness, const bool preferFlowControl) {
#if defined(_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    if (enableStrictness) {
        compileFlags |= D3DCOMPILE_ENABLE_STRICTNESS;
    }
    if (preferFlowControl) {
        compileFlags |= D3DCOMPILE_PREFER_FLOW_CONTROL;
    }
    return compileFlags;
#else
    return GetOptimizedD3D11ShaderCompileFlags(enableStrictness, preferFlowControl);
#endif
}

ID3DBlob* LoadPrecompiledShaderBlob(const wchar_t* csoFilename, std::string& error) {
    const std::filesystem::path shaderPath = ResolveShaderPath(csoFilename);
    std::ifstream stream(shaderPath, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "Precompiled shader not found: " + shaderPath.string();
        return nullptr;
    }

    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        error = "Precompiled shader is empty: " + shaderPath.string();
        return nullptr;
    }
    stream.seekg(0, std::ios::beg);

    ID3DBlob* shaderBlob = nullptr;
    const HRESULT result = D3DCreateBlob(static_cast<SIZE_T>(size), &shaderBlob);
    if (FAILED(result) || shaderBlob == nullptr) {
        error = FormatD3D11StageError("D3DCreateBlob(precompiled shader)", result);
        return nullptr;
    }

    if (!stream.read(static_cast<char*>(shaderBlob->GetBufferPointer()), size)) {
        shaderBlob->Release();
        error = "Failed to read precompiled shader: " + shaderPath.string();
        return nullptr;
    }

    error.clear();
    return shaderBlob;
}

ID3DBlob* CompileD3D11ShaderFromFile(
    const wchar_t* shaderFilename,
    const char* entryPoint,
    const char* target,
    const UINT compileFlags,
    std::string& error) {
    const std::wstring csoFilename = MakePrecompiledShaderFilename(shaderFilename, entryPoint, target);
    if (!ShouldForceRuntimeShaderCompilation()) {
        std::string precompiledError;
        if (ID3DBlob* precompiledBlob = LoadPrecompiledShaderBlob(csoFilename.c_str(), precompiledError)) {
            error.clear();
            return precompiledBlob;
        }
    }

    const std::filesystem::path shaderPath = ResolveShaderPath(shaderFilename);
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    const HRESULT result = D3DCompileFromFile(
        shaderPath.c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint,
        target,
        compileFlags,
        0,
        &shaderBlob,
        &errorBlob);

    if (errorBlob != nullptr) {
        error.assign(static_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        errorBlob->Release();
    } else {
        error.clear();
    }

    if (FAILED(result)) {
        if (shaderBlob != nullptr) {
            shaderBlob->Release();
        }
        if (error.empty()) {
            char buffer[512] {};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "D3DCompileFromFile(%ls, %s) failed (HRESULT 0x%08X)",
                shaderPath.c_str(),
                entryPoint,
                static_cast<unsigned int>(result));
            error = buffer;
        }
        return nullptr;
    }

    return shaderBlob;
}

bool CreateD3D11ComputeShaderFromFile(
    ID3D11Device* device,
    const wchar_t* shaderFilename,
    const char* entryPoint,
    const UINT compileFlags,
    const char* createStageName,
    ID3D11ComputeShader** shader,
    std::string& error) {
    if (shader == nullptr) {
        error = "CreateD3D11ComputeShaderFromFile received a null shader output pointer.";
        return false;
    }

    *shader = nullptr;
    ID3DBlob* shaderBlob = CompileD3D11ShaderFromFile(shaderFilename, entryPoint, "cs_5_0", compileFlags, error);
    if (shaderBlob == nullptr) {
        return false;
    }

    const HRESULT result = device->CreateComputeShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        shader);
    shaderBlob->Release();
    if (FAILED(result)) {
        error = FormatD3D11StageError(createStageName, result);
        return false;
    }

    error.clear();
    return true;
}

std::string FormatD3D11StageError(const char* stage, const HRESULT result) {
    char buffer[128] {};
    std::snprintf(buffer, sizeof(buffer), "%s failed (HRESULT 0x%08X)", stage, static_cast<unsigned int>(result));
    return buffer;
}

}  // namespace radiary
