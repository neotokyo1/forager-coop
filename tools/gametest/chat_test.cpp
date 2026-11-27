// Build for x86, with assertions enabled, linking native/src/chat.cpp.
#include "../../native/src/chat.h"

#include <cassert>
#include <map>

#include "../../native/src/character.h"
#include "../../native/src/session.h"
using namespace coop;
static std::map<std::string, std::string> globals;
static std::vector<net::Message> sent, wire;
static bool sendOk = true;
static auto role = session::Role::kHost;
namespace yyc {
RValue String(const char* s) {
    RValue v{};
    v.kind = kString;
    v.ptr = new std::string(s);
    return v;
}
std::string ToString(const RValue& v) { return v.kind == kString ? *static_cast<std::string*>(v.ptr) : ""; }
void Free(RValue* v) {
    if (v->kind == kString) delete static_cast<std::string*>(v->ptr);
    *v = {};
}
double ToReal(const RValue& v) { return v.real; }
bool CallBuiltin(const char* b, CInstance*, int, RValue* a, RValue* r) {
    auto key = ToString(a[0]);
    std::string fn = b;
    if (fn == "variable_global_set")
        globals[key] = ToString(a[1]);
    else if (fn == "variable_global_get")
        *r = String(globals[key].c_str());
    else
        r->real = globals.count(key);
    return true;
}
}  // namespace yyc
namespace coop::session {
Role CurrentRole() { return role; }
}  // namespace coop::session
namespace coop::character {
bool Introduced(net::PeerId p) { return p == 1 || p == 2; }
std::string OwnName() { return role == session::Role::kHost ? "Host" : "Alex"; }
std::string HostGuestName(net::PeerId) { return "Alex"; }
}  // namespace coop::character
namespace coop::net {
std::vector<PeerId> Peers() { return {1, 2}; }
void SendExcept(PeerId p, uint8_t t, const void* b, size_t n) {
    auto a = (const uint8_t*)b;
    sent.push_back({t, p, {a, a + n}});
}
bool SendTo(PeerId p, uint8_t t, const void* b, size_t n) {
    if (!sendOk) return false;
    SendExcept(p, t, b, n);
    return true;
}
std::vector<Message> DrainType(uint8_t) {
    auto copy = wire;
    wire.clear();
    return copy;
}
}  // namespace coop::net
int main() {
    globals["fc_chat_out_v1"] = "stale";
    chat::Tick(nullptr, true);
    assert(sent.empty());
    globals["fc_chat_out_v1"] = " Grüß dich!\n/dance\n   /give";
    chat::Tick(nullptr, true);
    assert(sent.size() == 1);
    assert(globals["fc_chat_in_v1"] == "Host:  Grüß dich!");
    globals["fc_chat_in_v1"].clear();
    auto hostLine = sent.back();
    assert(hostLine.from == 0 && hostLine.type == 120);
    wire.push_back({119, 1, {'h', 'i'}});
    chat::Tick(nullptr, true);
    assert(sent.size() == 2 && sent.back().from == 1);
    assert(globals["fc_chat_in_v1"] == "Alex: hi");
    chat::Handle({119, 3, {'x'}}, true);
    chat::Handle({119, 1, {0xc0, 0xaf}}, true);
    chat::Handle({119, 1, {'a', 0, 'b'}}, true);
    chat::Handle({119, 1, std::vector<uint8_t>(961, 'x')}, true);
    assert(sent.size() == 2);
    chat::Reset();
    role = session::Role::kGuest;
    chat::Tick(nullptr, true);
    globals["fc_chat_out_v1"] = "hey";
    chat::Tick(nullptr, true);
    assert(sent.size() == 3 && sent.back().type == 119);
    assert(globals["fc_chat_in_v1"] == "Alex: hey");
    globals["fc_chat_in_v1"].clear();
    sendOk = false;
    globals["fc_chat_out_v1"] = "unsent";
    chat::Tick(nullptr, true);
    assert(globals["fc_chat_in_v1"].empty());
    chat::Handle({120, 2, {1, 'A', 'x'}}, false);
    chat::Handle({120, 0, {30, 'x'}}, false);
    wire.push_back(hostLine);
    chat::Tick(nullptr, true);
    assert(globals["fc_chat_in_v1"] == "Host:  Grüß dich!" && sent.size() == 3);
    chat::Tick(nullptr, false);
    globals["fc_chat_out_v1"] = "offline";
    chat::Tick(nullptr, true);
    assert(sent.size() == 3);
}
