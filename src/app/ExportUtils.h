#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace radiary {

std::uint8_t FloatToByte(float value);
float HalfToFloat(std::uint16_t value);
std::uint32_t PackBgra(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a);
void CompositePixelsOver(std::vector<std::uint32_t>& destination, const std::vector<std::uint32_t>& source);

bool SavePixelsToImageFile(
    const std::filesystem::path& path,
    const std::vector<std::uint32_t>& pixels,
    int width,
    int height,
    bool jpeg);

std::filesystem::path FindBundledFfmpegPath();

std::wstring Utf8FileTextToWide(const std::string& value);
std::wstring ReadTextFile(const std::filesystem::path& path);

void ApplyDarkTitleBar(void* window);

}  // namespace radiary
