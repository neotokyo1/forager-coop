#include "trade_store.h"

#include <algorithm>
#include <cstring>
#include <fstream>

#include "files.h"

namespace coop::trade {
namespace fs = std::filesystem;
namespace {
constexpr char kJournal[] = "trade.pending";

bool Valid(const Image& image) {
    const auto& name = image.guestFile;
    if (!name.empty()) {
        if (name.size() > 80 || name.size() < 5 || name.substr(name.size() - 4) != ".cin") return false;
        if (!std::all_of(name.begin(), name.end() - 4, [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            }))
            return false;
        if (fs::u8path(name).filename().u8string() != name || name.find_first_of("/\\:") != name.npos ||
            name.find("..") != name.npos || name.find('\0') != name.npos)
            return false;
    }
    return image.bytes.size() <= kMaxImage && files::IsCompleteCinder(image.bytes.data(), image.bytes.size());
}

void Put(Bytes& out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
bool Read(std::istream& in, Image& image) {
    unsigned char header[5];
    if (!in.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
    uint32_t size = 0;
    for (int i = 0; i < 4; ++i) size |= uint32_t(header[i + 1]) << (8 * i);
    if (header[0] > 80 || !size || size > kMaxImage) return false;
    image.guestFile.resize(header[0]);
    image.bytes.resize(size);
    return in.read(image.guestFile.data(), header[0]) && in.read(reinterpret_cast<char*>(image.bytes.data()), size) &&
           Valid(image);
}
}  // namespace
bool CommitImages(const fs::path& guests, const std::array<Image, 2>& images) {
    std::error_code ec;
    if (guests.empty() || fs::exists(guests / kJournal, ec) || ec || !Valid(images[0]) || !Valid(images[1]) ||
        images[0].guestFile == images[1].guestFile)
        return false;
    Bytes data{'T', 'R', 'D', '1'};
    for (const Image& image : images) {
        data.push_back(static_cast<uint8_t>(image.guestFile.size()));
        Put(data, static_cast<uint32_t>(image.bytes.size()));
        data.insert(data.end(), image.guestFile.begin(), image.guestFile.end());
        data.insert(data.end(), image.bytes.begin(), image.bytes.end());
    }
    return files::WriteAtomic(guests / kJournal, data.data(), data.size());
}

bool InstallImages(const fs::path& guests) {
    if (guests.empty()) return false;
    const fs::path journal = guests / kJournal;
    std::error_code ec;
    if (!fs::exists(journal, ec)) return !ec;
    std::ifstream in(journal, std::ios::binary);
    char magic[4];
    std::array<Image, 2> images;
    if (!in.read(magic, 4) || std::memcmp(magic, "TRD1", 4) || !Read(in, images[0]) || !Read(in, images[1]) ||
        in.peek() != EOF || images[0].guestFile == images[1].guestFile)
        return false;
    in.close();
    for (const Image& image : images) {
        const auto dest =
            image.guestFile.empty() ? guests.parent_path() / "Data.cin" : guests / fs::u8path(image.guestFile);
        if (image.guestFile.empty() &&
            !files::WriteAtomic(guests / "trade-host.checkpoint", image.bytes.data(), image.bytes.size()))
            return false;
        if (!files::WriteAtomic(dest, image.bytes.data(), image.bytes.size())) return false;
    }
    return fs::remove(journal, ec) && !ec;
}
bool RestoreHostImage(const fs::path& guests) {
    const auto mirror = guests / "trade-host.checkpoint";
    std::error_code ec;
    if (!fs::exists(mirror, ec)) return !ec;
    auto length = fs::file_size(mirror, ec);
    if (ec || length > kMaxImage) return false;
    std::ifstream in(mirror, std::ios::binary);
    Bytes bytes(static_cast<size_t>(length));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), length) || !files::IsCompleteCinder(bytes.data(), bytes.size()))
        return false;
    in.close();
    if (!files::WriteAtomic(guests.parent_path() / "Data.cin", bytes.data(), bytes.size())) return false;
    return fs::remove(mirror, ec) && !ec;
}

}  // namespace coop::trade
