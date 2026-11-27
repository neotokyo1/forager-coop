#include "chat.h"

#include <algorithm>
#include <deque>
#include <string>

#include "character.h"
#include "session.h"

namespace coop::chat {
namespace {
constexpr char kOutgoing[] = "fc_chat_out_v1", kIncoming[] = "fc_chat_in_v1";
constexpr size_t kMaxBytes = 960, kMaxChars = 240, kMaxName = 40;
std::deque<std::string> incoming;
bool connectedLastStep = false;

// Reject invalid UTF-8, control characters, and command lines at the receiving boundary too.
bool Valid(const std::string& value) {
    auto start = value.find_first_not_of(' ');
    if (value.size() > kMaxBytes || start == value.npos || value[start] == '/') return false;
    size_t chars = 0;
    for (size_t i = 0; i < value.size();) {
        unsigned char c = value[i++];
        if (++chars > kMaxChars || c < 32 || c == 127) return false;
        if (c < 128) continue;
        if (c < 0xc2 || c > 0xf4) return false;
        unsigned n = c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
        if (value.size() - i < n) return false;
        unsigned char next = value[i];
        if ((c == 0xe0 && next < 0xa0) || (c == 0xed && next >= 0xa0) || (c == 0xf0 && next < 0x90) ||
            (c == 0xf4 && next >= 0x90) || (c == 0xc2 && next < 0xa0))
            return false;
        for (unsigned k = 0; k < n; ++k)
            if ((static_cast<unsigned char>(value[i++]) & 0xc0) != 0x80) return false;
    }
    return true;
}

void Display(const std::string& name, const std::string& words) {
    if (incoming.size() == 32) incoming.pop_front();
    incoming.push_back(name + ": " + words);
}
void Broadcast(net::PeerId from, const std::string& words) {
    const std::string name = from ? character::HostGuestName(from) : character::OwnName();
    if (name.empty() || name.size() > kMaxName) return;
    std::string line(1, static_cast<char>(name.size()));
    line += name;
    line += words;
    // Guests display their own outgoing line locally when SendTo succeeds.
    net::SendExcept(from, kLine, line.data(), line.size());
    Display(name, words);
}

std::string Read(yyc::CInstance* player, const char* key) {
    yyc::RValue name = yyc::String(key), value{};
    yyc::CallBuiltin("variable_global_get", player, 1, &name, &value);
    auto text = yyc::ToString(value);
    yyc::Free(&value);
    yyc::Free(&name);
    return text;
}
void Write(yyc::CInstance* player, const char* key, const std::string& text) {
    yyc::RValue args[] = {yyc::String(key), yyc::String(text.c_str())}, result{};
    yyc::CallBuiltin("variable_global_set", player, 2, args, &result);
    yyc::Free(&result);
    yyc::Free(&args[0]);
    yyc::Free(&args[1]);
}
bool HasFrontend(yyc::CInstance* player) {
    yyc::RValue name = yyc::String(kOutgoing), result{};
    yyc::CallBuiltin("variable_global_exists", player, 1, &name, &result);
    bool exists = yyc::ToReal(result) != 0;
    yyc::Free(&name);
    yyc::Free(&result);
    return exists;
}
}  // namespace

void Handle(const net::Message& message, bool host) {
    const auto& bytes = message.payload;
    if (host && message.type == kSubmit && message.from && bytes.size() <= kMaxBytes &&
        character::Introduced(message.from)) {
        const auto peers = net::Peers();
        if (std::find(peers.begin(), peers.end(), message.from) == peers.end()) return;
        std::string words(bytes.begin(), bytes.end());
        if (Valid(words)) Broadcast(message.from, words);
    } else if (!host && message.type == kLine && !message.from && !bytes.empty()) {
        size_t n = bytes.front();
        if (!n || n > kMaxName || bytes.size() <= n + 1 || bytes.size() - n - 1 > kMaxBytes) return;
        std::string name(bytes.begin() + 1, bytes.begin() + 1 + n), words(bytes.begin() + 1 + n, bytes.end());
        if (Valid(name) && Valid(words)) Display(name, words);
    }
}
void Tick(yyc::CInstance* player, bool connected) {
    const bool host = session::CurrentRole() == session::Role::kHost;
    if (connected) {
        for (auto& m : net::DrainType(host ? kSubmit : kLine)) Handle(m, host);
    }
    const bool changed = connected != connectedLastStep;
    connectedLastStep = connected;
    if (!connected) incoming.clear();
    if ((!connected && !changed) || !HasFrontend(player)) return;
    if (changed) {
        Write(player, kOutgoing, "");
        Write(player, kIncoming, "");
        return;
    }
    auto batch = Read(player, kOutgoing);
    if (!batch.empty()) {
        Write(player, kOutgoing, "");
        size_t start = 0;
        while (start < batch.size()) {
            auto end = batch.find('\n', start);
            auto words = batch.substr(start, end == batch.npos ? end : end - start);
            if (Valid(words)) {
                if (host)
                    Broadcast(0, words);
                else if (net::SendTo(0, kSubmit, words.data(), words.size()))
                    Display(character::OwnName(), words);
            }
            if (end == batch.npos) break;
            start = end + 1;
        }
    }
    if (!incoming.empty() && Read(player, kIncoming).empty()) {
        Write(player, kIncoming, incoming.front());
        incoming.pop_front();
    }
}
void Reset() {
    incoming.clear();
    connectedLastStep = false;
}
}  // namespace coop::chat
