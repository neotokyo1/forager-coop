#pragma once

#include "net.h"
#include "yyc.h"

namespace coop::trade {

// Called on the local player before its normal input step.
bool Input(yyc::CInstance* player);
bool Open();
void Draw(yyc::CInstance* control);
bool Handle(const net::Message& message, yyc::CInstance* player);
void PeerLeft(net::PeerId peer);
void Reset();
// Finish installing both images of a committed exchange before loading this slot.
bool Recover();
bool BeforeLoad();
bool AcceptCharacterSave(net::PeerId peer);
}  // namespace coop::trade
