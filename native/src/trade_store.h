#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace coop::trade {
using Bytes = std::vector<uint8_t>;
constexpr size_t kMaxImage = 3 * 1024 * 1024;
struct Image {
    // Empty for the host. Guest filenames are chosen locally by character.cpp.
    std::string guestFile;
    Bytes bytes;
};

bool CommitImages(const std::filesystem::path& guests, const std::array<Image, 2>& images);
bool RestoreHostImage(const std::filesystem::path& guests);
bool InstallImages(const std::filesystem::path& guests);
}  // namespace coop::trade
