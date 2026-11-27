#include "trade_bag.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "files.h"
#include "log.h"
#include "trade_capture.h"
#include "world.h"

namespace coop::trade {
namespace fs = std::filesystem;
namespace {
constexpr int kInventory = 0x5A4, kCapacity = 0x5A6;

int Integer(const yyc::RValue& v, int max) {
    if (!yyc::IsNumber(v)) return -1;
    double n = yyc::ToReal(v);
    return n >= 0 && n <= max && n == std::floor(n) ? static_cast<int>(n) : -1;
}
int Length(yyc::CInstance* p, yyc::RValue& v) {
    if ((v.kind & yyc::kKindMask) != yyc::kArray) return -1;
    yyc::RValue len{};
    bool ok = yyc::CallBuiltin("array_length_1d", p, 1, &v, &len);
    int result = ok ? Integer(len, kMaxSlots) : -1;
    yyc::Free(&len);
    return result;
}

BagLayout Inspect(yyc::CInstance* p, int id, yyc::RValue& inventory) {
    BagLayout bag;
    yyc::RValue capacity{};
    yyc::GetVar(id, kCapacity, &capacity);
    bag.capacity = Integer(capacity, kMaxSlots);
    yyc::Free(&capacity);
    const int physicalLength = Length(p, inventory);
    if (physicalLength < 0 || bag.capacity < 0) return {};
    bag.length = (std::min)(physicalLength, bag.capacity);
    for (int i = 0; i < bag.length; ++i) {
        auto row = yyc::ArrayElement(&inventory, i);
        if (Length(p, row) != 2) continue;
        const auto item = yyc::ArrayElement(&row, 0);
        int count = Integer(yyc::ArrayElement(&row, 1), kMaxCount);
        int type = Integer(item, 65535);
        if (type >= 0 && count > 0)
            bag.stacks.push_back({i, type, count});
        else if (yyc::IsNumber(item) && yyc::ToReal(item) < 0 && count == 0)
            bag.empty = i;
    }
    return bag;
}

// The engine accessor grows arrays on out-of-bounds access. Only use it on
// arrays we just allocated, with the verified length, and move owned RValues.
bool NewArray(yyc::CInstance* p, int n, yyc::RValue& out) {
    auto size = yyc::Real(n);
    return yyc::CallBuiltin("array_create", p, 1, &size, &out) && Length(p, out) == n;
}
bool MoveCell(yyc::RValue& array, int i, yyc::RValue& owned) {
    using Cell = yyc::RValue*(__thiscall*)(yyc::RValue*, int);
    auto dest = yyc::At<Cell>(yyc::va::ArrayIndex)(&array, i);
    if (!dest) return false;
    yyc::Free(dest);
    *dest = owned;
    owned = {};
    return true;
}
bool MakeInventory(yyc::CInstance* p, int id, yyc::RValue& original, const Offer& give, const Offer& receive,
                   yyc::RValue& after) {
    const auto bag = Inspect(p, id, original);
    const auto changes = Plan(bag, give, receive);
    if (changes.write.empty()) return false;
    // Keep preallocated but locked slots byte-for-byte, outside the selectable range.
    const int physicalLength = Length(p, original) - bag.length + changes.length;
    if (!NewArray(p, physicalLength, after)) return false;
    for (int i = 0; i < physicalLength; ++i) {
        int from = i + (changes.erase >= 0 && i >= changes.erase);
        auto replacement =
            std::find_if(changes.write.begin(), changes.write.end(), [from](Stack row) { return row.slot == from; });
        yyc::RValue row{};
        bool ok;
        if (replacement != changes.write.end()) {
            auto item = yyc::Real(replacement->item), amount = yyc::Real(replacement->count);
            ok = NewArray(p, 2, row) && MoveCell(row, 0, item) && MoveCell(row, 1, amount);
        } else {
            ok = yyc::GetVarAt(id, kInventory, from == changes.clear ? bag.empty : from, &row);
        }
        if (ok) ok = MoveCell(after, i, row);
        yyc::Free(&row);
        if (!ok) return false;
    }
    return true;
}

// Keep one scratch slot for the session. The marker handle excludes other game
// instances. Only a directory created by this feature can be reclaimed after a crash.
HANDLE scratchLock = INVALID_HANDLE_VALUE;
int scratchNumber = -1;
bool ReserveScratch() {
    if (scratchLock != INVALID_HANDLE_VALUE) return true;
    const fs::path root = fs::path(world::GuestSlotPath()).parent_path();
    if (root.empty()) return false;
    for (int number = 90; number < world::kGuestSlot; ++number) {
        const auto candidate = root / ("saveData" + std::to_string(number));
        auto* current = yyc::GlobalVar(yyc::slot::saveFile);
        if (!current || !yyc::IsNumber(*current) || yyc::ToReal(*current) == number) continue;
        std::error_code ec;
        bool created = fs::create_directory(candidate, ec);
        if (ec) continue;
        auto marker = candidate / "coop-trade-scratch.lock";
        DWORD attributes = GetFileAttributesW(candidate.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        HANDLE lock = CreateFileW(marker.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                  created ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE) continue;
        scratchLock = lock;
        scratchNumber = number;
        return true;
    }
    return false;
}
// Keep SEH cleanup in a leaf without C++ objects requiring unwinding.
void SaveProjectedSlot(yyc::CInstance* p, Bytes* image, yyc::RValue old) {
    BeginCapture(image);
    __try {
        *yyc::GlobalVar(yyc::slot::saveFile) = yyc::Real(scratchNumber);
        world::GuestSaveCharacter(p);
    } __finally {
        *yyc::GlobalVar(yyc::slot::saveFile) = old;
        EndCapture();
    }
}
Bytes StageSave(yyc::CInstance* p) {
    if (!InstallCapture() || !ReserveScratch()) return {};
    auto* slot = yyc::GlobalVar(yyc::slot::saveFile);
    if (!slot || !yyc::IsNumber(*slot)) return {};
    auto old = *slot;
    Bytes image;
    SaveProjectedSlot(p, &image, old);
    if (image.empty()) Log("trade: SaveGame did not submit a complete Data.cin during inventory projection");
    // A serializer that defers reading the player until a later event cannot
    // vote: it never submitted Data.cin while the projected bag was installed.
    return image;
}
}  // namespace
void ReleaseScratch() {
    if (scratchLock == INVALID_HANDLE_VALUE) return;
    // Leave the owned directory and marker for the next session. The engine
    // can still be finishing asynchronous zone writes when a player exits.
    CloseHandle(scratchLock);
    scratchLock = INVALID_HANDLE_VALUE;
    scratchNumber = -1;
}
std::vector<Stack> Bag(yyc::CInstance* p) {
    const int id = static_cast<int>(yyc::GetBuiltinReal(p, yyc::va::YYVar_id));
    yyc::RValue inventory{};
    yyc::GetVar(id, kInventory, &inventory);
    auto bag = Inspect(p, id, inventory);
    yyc::Free(&inventory);
    return bag.stacks;
}
bool CanExchange(yyc::CInstance* p, const Offer& give, const Offer& receive) {
    const int id = static_cast<int>(yyc::GetBuiltinReal(p, yyc::va::YYVar_id));
    yyc::RValue before{};
    yyc::GetVar(id, kInventory, &before);
    auto plan = Plan(Inspect(p, id, before), give, receive);
    yyc::Free(&before);
    return !plan.write.empty();
}
void CaptureProjection(yyc::CInstance* p, Bytes* image) { *image = StageSave(p); }
bool CaptureSafely(yyc::CInstance* p, Bytes* image) {
    __try {
        CaptureProjection(p, image);
        return !image->empty();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("trade: checkpoint failed; restoring the original inventory");
        image->clear();
        return false;
    }
}
bool Change(yyc::CInstance* p, const Offer& give, const Offer& receive, Bytes* checkpoint) {
    const int id = static_cast<int>(yyc::GetBuiltinReal(p, yyc::va::YYVar_id));
    yyc::RValue before{}, after{};
    bool ok = yyc::GetVar(id, kInventory, &before) && MakeInventory(p, id, before, give, receive, after);
    if (ok) {
        yyc::SetVar(id, kInventory, &after);
        yyc::RValue check{};
        ok = yyc::GetVar(id, kInventory, &check) && check.ptr == after.ptr;
        yyc::Free(&check);
        if (ok && checkpoint) ok = CaptureSafely(p, checkpoint);
        if (checkpoint || !ok) yyc::SetVar(id, kInventory, &before);
    }
    yyc::Free(&before);
    yyc::Free(&after);
    return ok;
}
Bytes Checkpoint(yyc::CInstance* p, const Offer& give, const Offer& receive) {
    Bytes result;
    Change(p, give, receive, &result);
    return result;
}
bool Exchange(yyc::CInstance* p, const Offer& give, const Offer& receive) { return Change(p, give, receive, nullptr); }
}  // namespace coop::trade
