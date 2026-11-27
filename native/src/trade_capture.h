#pragma once
#include "trade_store.h"
#include "yyc.h"

namespace coop::trade {
// Capture the immutable Data.cin buffer at submission, while the projected bag
// is installed. The game's asynchronous disk completion never votes on a trade.
bool InstallCapture();
void EnterSave();
void LeaveSave();
void BeginCapture(Bytes* target);
void EndCapture();
void CaptureGuestSaves(const Bytes& committed);
const Bytes* LatestGuestSave();
void ResetCapture();
void TrackHostSaves(const std::filesystem::path& guests);
}  // namespace coop::trade
