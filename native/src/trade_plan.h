#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace coop::trade {
constexpr int kMaxSlots = 512;
constexpr int kMaxCount = 1000000000;
struct Stack {
    int32_t slot = -1, item = -1, count = 0;
    int32_t extraCount = 0;
    std::array<int32_t, 8> extra{};  // integral item attributes following the amount
    bool SameAttributes(const Stack& other) const { return extraCount == other.extraCount && extra == other.extra; }
    Stack At(int dest, int quantity) const {
        Stack copy = *this;
        copy.slot = dest;
        copy.count = quantity;
        return copy;
    }
};
struct Offer {
    Stack stack;
    int32_t amount = 0;
};
inline bool ValidOffer(const Offer& o) {
    return o.stack.slot >= 0 && o.stack.slot < kMaxSlots && o.stack.item >= 0 && o.stack.item <= 65535 &&
           o.stack.extraCount >= 0 && o.stack.extraCount <= static_cast<int>(o.stack.extra.size()) && o.amount > 0 &&
           o.amount <= o.stack.count && o.stack.count <= kMaxCount;
}

struct BagLayout {
    int length = 0, capacity = 0;
    int empty = -1;  // an observed empty row that can also be copied to a newly vacated slot
    std::vector<Stack> stacks;
};
struct Changes {
    std::vector<Stack> write;
    int clear = -1, erase = -1;
    int length = 0;
};
inline Changes Plan(const BagLayout& bag, const Offer& give, const Offer& receive) {
    if (!ValidOffer(give) || !ValidOffer(receive) || give.stack.item == receive.stack.item || bag.length < 1 ||
        bag.capacity < bag.length || bag.capacity > kMaxSlots)
        return {};
    const Stack* source = nullptr;
    const Stack* target = nullptr;
    for (const auto& row : bag.stacks) {
        if (row.slot == give.stack.slot && row.item == give.stack.item && row.count == give.stack.count &&
            row.SameAttributes(give.stack))
            source = &row;
        if (row.item == receive.stack.item) {
            if (!row.SameAttributes(receive.stack)) return {};
            target = &row;
        }
    }
    if (!source) return {};
    Changes result;
    result.length = bag.length;
    const bool whole = give.amount == source->count;
    const bool compact =
        !bag.stacks.empty() && static_cast<int>(bag.stacks.size()) == bag.length && bag.length < bag.capacity;
    if (whole && !target) {
        result.write.push_back(receive.stack.At(source->slot, receive.amount));
        return result;
    }
    if (whole) {
        if (bag.empty >= 0)
            result.clear = source->slot;
        else if (compact) {
            result.erase = source->slot;
            --result.length;
        } else
            return {};
    } else {
        result.write.push_back(source->At(source->slot, source->count - give.amount));
    }
    if (target) {
        if (target->count > kMaxCount - receive.amount) return {};
        result.write.push_back(target->At(target->slot, target->count + receive.amount));
    } else {
        int dest = bag.empty;
        if (dest < 0 && compact) dest = result.length++;
        if (dest < 0) return {};
        result.write.push_back(receive.stack.At(dest, receive.amount));
    }
    return result;
}
}  // namespace coop::trade
