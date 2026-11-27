#include "trade_capture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <string>

#include "MinHook.h"
#include "character.h"
#include "files.h"
#include "log.h"
#include "world.h"

namespace coop::trade {
namespace {
constexpr const char* names[] = {"buffer_save", "buffer_save_ext", "buffer_save_async"};
std::array<yyc::BuiltinFn, 3> original{};
unsigned saveDepth = 0;
Bytes* staging = nullptr;
Bytes guestSave;
std::filesystem::path hostDirectory;

bool DataFile(const yyc::RValue& path) {
    auto name = yyc::ToString(path);
    auto slash = name.find_last_of("/\\");
    if (slash != name.npos) name.erase(0, slash + 1);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return name == "data.cin";
}
Bytes ImageOf(yyc::CInstance* self, int argc, yyc::RValue* args) {
    double buffer = yyc::ToReal(args[0]);
    double capacity = yyc::CallReal(self, "buffer_get_size", {buffer});
    double offset = argc >= 4 ? yyc::ToReal(args[2]) : 0;
    double length = argc >= 4 ? yyc::ToReal(args[3]) : capacity;
    if (length == -1) length = capacity - offset;
    if (!(offset >= 0 && length > 0 && length <= kMaxImage && offset + length <= capacity) ||
        offset != std::floor(offset) || length != std::floor(length))
        return {};
    yyc::RValue addr{};
    yyc::CallBuiltin("buffer_get_address", self, 1, args, &addr);
    Bytes image;
    if ((addr.kind & yyc::kKindMask) == yyc::kPtr && addr.ptr) {
        auto data = static_cast<const uint8_t*>(addr.ptr) + static_cast<size_t>(offset);
        auto size = static_cast<size_t>(length);
        if (files::IsCompleteCinder(data, size)) image.assign(data, data + size);
    }
    yyc::Free(&addr);
    return image;
}
template <size_t I>
void __cdecl Capture(yyc::RValue* r, yyc::CInstance* self, yyc::CInstance* other, int argc, yyc::RValue* args) {
    if ((staging || (saveDepth && (!guestSave.empty() || !hostDirectory.empty()))) && argc >= 2 && DataFile(args[1])) {
        const auto* slot = yyc::GlobalVar(yyc::slot::saveFile);
        const bool guestSlot = slot && yyc::IsNumber(*slot) && yyc::ToReal(*slot) == world::kGuestSlot;
        const bool hostSlot =
            !hostDirectory.empty() &&
            std::filesystem::path(character::WorldNoteFile("trade.pending")).parent_path() == hostDirectory;
        auto image = staging || (!guestSave.empty() && guestSlot) || hostSlot ? ImageOf(self, argc, args) : Bytes{};
        if (!image.empty()) {
            if (staging)
                *staging = std::move(image);
            else if (!guestSave.empty())
                guestSave = std::move(image);
            else if (!hostDirectory.empty() &&
                     !files::WriteAtomic(hostDirectory / "trade-host.checkpoint", image.data(), image.size())) {
                Log("trade: host checkpoint write failed");
                // The game's own writer below still saves. Disarm the older mirror if possible.
                std::error_code ec;
                std::filesystem::remove(hostDirectory / "trade-host.checkpoint", ec);
            }
        }
    }
    original[I](r, self, other, argc, args);
}
}  // namespace
bool InstallCapture() {
    static bool done = false;
    if (done) return true;
    const yyc::BuiltinFn hooks[] = {Capture<0>, Capture<1>, Capture<2>};
    bool any = false;
    for (size_t i = 0; i < original.size(); ++i) {
        auto addr = reinterpret_cast<void*>(yyc::BuiltinAddress(names[i]));
        if (!addr) continue;
        if (!original[i] &&
            MH_CreateHook(addr, reinterpret_cast<void*>(hooks[i]), reinterpret_cast<void**>(&original[i])) != MH_OK)
            return false;
        auto status = MH_EnableHook(addr);
        if (status != MH_OK && status != MH_ERROR_ENABLED) return false;
        any = true;
    }
    done = any;
    return done;
}
void EnterSave() { ++saveDepth; }
void LeaveSave() { --saveDepth; }
void BeginCapture(Bytes* target) { staging = target; }
void EndCapture() { staging = nullptr; }
void CaptureGuestSaves(const Bytes& committed) { guestSave = committed; }
const Bytes* LatestGuestSave() { return guestSave.empty() ? nullptr : &guestSave; }
void TrackHostSaves(const std::filesystem::path& dir) { hostDirectory = dir; }
void ResetCapture() {
    staging = nullptr;
    guestSave.clear();
    // A clean exit already wrote this exact generation. Leave the recovery copy
    // only if the asynchronous writer has not installed it yet.
    if (!hostDirectory.empty()) {
        const auto mirror = hostDirectory / "trade-host.checkpoint";
        const auto normal = hostDirectory.parent_path() / "Data.cin";
        std::error_code ec;
        const auto n = std::filesystem::file_size(mirror, ec);
        if (!ec && n > 0 && n <= kMaxImage && std::filesystem::file_size(normal, ec) == n && !ec) {
            Bytes a(static_cast<size_t>(n)), b(a.size());
            std::ifstream first(mirror, std::ios::binary), second(normal, std::ios::binary);
            if (first.read(reinterpret_cast<char*>(a.data()), n) && second.read(reinterpret_cast<char*>(b.data()), n) &&
                a == b) {
                first.close();
                std::filesystem::remove(mirror, ec);
            }
        }
    }
    hostDirectory.clear();
}
}  // namespace coop::trade
