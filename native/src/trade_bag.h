#pragma once
#include "trade_plan.h"
#include "trade_store.h"
#include "yyc.h"

namespace coop::trade {
std::vector<Stack> Bag(yyc::CInstance* player);
void ReleaseScratch();
bool CanExchange(yyc::CInstance* player, const Offer& give, const Offer& receive);
// Captures the game's own post-exchange Data.cin in a newly reserved working slot.
// All live edits are undone before returning. The caller keeps the game thread at
// this barrier until the host decides, so the validated stacks cannot be spent.
Bytes Checkpoint(yyc::CInstance* player, const Offer& give, const Offer& receive);
bool Exchange(yyc::CInstance* player, const Offer& give, const Offer& receive);
}  // namespace coop::trade
