#pragma once

#include <cstdint>
#include <vector>

namespace coop::trade {
constexpr int kMaxSlots = 512;
constexpr int kMaxCount = 1000000000;
struct Stack {
    int32_t slot = -1, item = -1, count = 0;
};
struct Offer {
    Stack stack;
    int32_t amount = 0;
};
inline bool ValidOffer(const Offer& o) {
    return o.stack.slot >= 0 && o.stack.slot < kMaxSlots && o.stack.item >= 0 && o.stack.item <= 65535 &&
           o.amount > 0 && o.amount <= o.stack.count && o.stack.count <= kMaxCount;
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
        if (row.slot == give.stack.slot && row.item == give.stack.item && row.count == give.stack.count) source = &row;
        if (row.item == receive.stack.item) target = &row;
    }
    if (!source) return {};
    Changes result;
    result.length = bag.length;
    const bool whole = give.amount == source->count;
    const bool compact =
        !bag.stacks.empty() && static_cast<int>(bag.stacks.size()) == bag.length && bag.length < bag.capacity;
    if (whole && !target) {
        result.write.push_back({source->slot, receive.stack.item, receive.amount});
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
        result.write.push_back({source->slot, source->item, source->count - give.amount});
    }
    if (target) {
        if (target->count > kMaxCount - receive.amount) return {};
        result.write.push_back({target->slot, target->item, target->count + receive.amount});
    } else {
        int dest = bag.empty;
        if (dest < 0 && compact) dest = result.length++;
        if (dest < 0) return {};
        result.write.push_back({dest, receive.stack.item, receive.amount});
    }
    return result;
}
}  // namespace coop::trade
