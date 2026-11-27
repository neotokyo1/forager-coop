// Standalone C++17 regression test. Link with native/src/trade_store.cpp and files.cpp.
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

#include "../../native/src/trade_plan.h"
#include "../../native/src/trade_store.h"

using namespace coop::trade;
namespace fs = std::filesystem;

Bytes Cinder(uint8_t marker) {
    Bytes b{'c', 'n', 'd', 'r', 1, 0, 1, 0, 0, 0, 'T', 'E', 'S', 'T', 1, 0, 0, 0, 22, 0, 0, 0, marker};
    return b;
}
Bytes Read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
void Journal() {
    auto dir = fs::temp_directory_path() /
               ("forager-trade-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(dir));
    auto guests = dir / "coop_guests";
    auto target = guests / "alex.cin";
    std::array<Image, 2> pair = {Image{"", Cinder(1)}, Image{"alex.cin", Cinder(2)}};
    assert(CommitImages(guests, pair));
    assert(!CommitImages(guests, pair));
    fs::create_directory(target);  // force the SECOND replacement to fail
    assert(!InstallImages(guests));
    assert(Read(dir / "Data.cin") == pair[0].bytes && fs::exists(guests / "trade.pending"));
    fs::remove(target);
    assert(InstallImages(guests) && InstallImages(guests));
    assert(Read(target) == pair[1].bytes && !fs::exists(guests / "trade.pending"));
    {
        std::ofstream stale(dir / "Data.cin", std::ios::binary | std::ios::trunc);
        stale << "old";
    }
    assert(RestoreHostImage(guests));
    assert(Read(dir / "Data.cin") == pair[0].bytes && !fs::exists(guests / "trade-host.checkpoint"));
    pair[0].guestFile = "sam.cin";  // guest-to-guest
    assert(CommitImages(guests, pair) && InstallImages(guests));
    assert(Read(guests / "sam.cin") == pair[0].bytes);
    pair[0].guestFile = "../Data.cin";
    assert(!CommitImages(guests, pair));
    pair[0] = pair[1];
    assert(!CommitImages(guests, pair));
    pair[0] = {"sam.cin", Bytes{1, 2, 3}};
    assert(!CommitImages(guests, pair));
    fs::remove_all(dir);
}

void Inventory() {
    for (int count = 1; count <= 5; ++count)
        for (int amount = 1; amount <= count; ++amount)
            for (int receive = 1; receive <= 5; ++receive)
                for (bool existing : {false, true})
                    for (bool hole : {false, true})
                        for (bool spare : {false, true}) {
                            BagLayout bag;
                            std::vector<Stack> rows{{0, 5, count}};
                            if (existing) rows.push_back({1, 7, 3});
                            bag.stacks = rows;
                            if (hole) {
                                bag.empty = static_cast<int>(rows.size());
                                rows.push_back({bag.empty, -1, 0});
                            }
                            bag.length = static_cast<int>(rows.size());
                            bag.capacity = bag.length + int(spare);
                            Offer give{rows[0], amount}, take{{0, 7, receive}, receive};
                            const auto plan = Plan(bag, give, take);
                            const bool possible =
                                (amount == count && !existing) || (amount < count && existing) || hole || spare;
                            assert(plan.write.empty() != possible);
                            if (!possible) continue;
                            for (Stack change : plan.write) {
                                if (change.slot == static_cast<int>(rows.size()))
                                    rows.push_back(change);
                                else
                                    rows.at(change.slot) = change;
                            }
                            if (plan.clear >= 0) rows.at(plan.clear) = {-1, -1, 0};
                            if (plan.erase >= 0) rows.erase(rows.begin() + plan.erase);
                            assert(static_cast<int>(rows.size()) == plan.length && plan.length <= bag.capacity);
                            std::map<int, int> totals;
                            for (Stack s : rows) totals[s.item] += s.count;
                            assert(totals[5] == count - amount && totals[7] == receive + (existing ? 3 : 0));
                        }
    Stack wood{0, 5, 4}, stone{1, 7, 3};
    wood.extraCount = stone.extraCount = 1;
    wood.extra[0] = 13;
    stone.extra[0] = -4;
    BagLayout single{1, 1, -1, {wood}};
    auto swap = Plan(single, {wood, 4}, {stone, 3});
    assert(swap.write.size() == 1 && swap.write[0].SameAttributes(stone));
    single.stacks[0].extra[0] = 14;
    assert(Plan(single, {wood, 4}, {stone, 3}).write.empty());
    BagLayout bag{2, 2, -1, {{0, 5, 4}, {1, 7, kMaxCount}}};
    assert(Plan(bag, {{0, 5, 3}, 1}, {{0, 7, 4}, 1}).write.empty());  // quantity changed
    assert(Plan(bag, {{0, 7, 4}, 1}, {{0, 5, 4}, 1}).write.empty());  // slot reused
    assert(Plan(bag, {{0, 5, 4}, 1}, {{0, 7, 4}, 1}).write.empty());  // overflow
    assert(!ValidOffer({{0, 1, 10}, 0}) && !ValidOffer({{0, 1, 10}, 11}));
}
int main() {
    Inventory();
    Journal();
}
