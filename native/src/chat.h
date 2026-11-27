#pragma once

#include "net.h"
#include "yyc.h"

namespace coop::chat {
constexpr uint8_t kSubmit = 119, kLine = 120;
void Tick(yyc::CInstance* player, bool connected);
void Handle(const net::Message& message, bool host);
void Reset();
}  // namespace coop::chat
