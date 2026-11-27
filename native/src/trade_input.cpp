#include "trade_input.h"

#include <array>
#include <utility>

#include "MinHook.h"
#include "yyc.h"

namespace coop::trade::input {
namespace {
constexpr const char* names[] = {"keyboard_check_pressed",
                                 "keyboard_check",
                                 "keyboard_check_released",
                                 "mouse_check_button_pressed",
                                 "mouse_check_button",
                                 "mouse_check_button_released",
                                 "gamepad_button_check",
                                 "gamepad_button_check_pressed",
                                 "gamepad_button_check_released",
                                 "gamepad_axis_value",
                                 "mouse_wheel_up",
                                 "mouse_wheel_down"};
constexpr auto count = std::size(names);
std::array<yyc::BuiltinFn, count> originals{};
bool capture = false;
unsigned readers = 0;
template <size_t I>
void __cdecl Filter(yyc::RValue* result, yyc::CInstance* self, yyc::CInstance* other, int argc, yyc::RValue* args) {
    if (capture && !readers) {
        yyc::Free(result);
        *result = yyc::Real(0);
        return;
    }
    originals[I](result, self, other, argc, args);
}
template <size_t... I>
auto Filters(std::index_sequence<I...>) {
    return std::array<yyc::BuiltinFn, count>{&Filter<I>...};
}
}  // namespace
bool Install() {
    static bool done = false;
    if (done) return true;
    static const auto filters = Filters(std::make_index_sequence<count>{});
    for (size_t i = 0; i < count; ++i) {
        auto addr = reinterpret_cast<void*>(yyc::BuiltinAddress(names[i]));
        if (!addr && i >= 6) continue;
        if (!addr) return false;
        if (!originals[i] &&
            MH_CreateHook(addr, reinterpret_cast<void*>(filters[i]), reinterpret_cast<void**>(&originals[i])) != MH_OK)
            return false;
        auto status = MH_EnableHook(addr);
        if (status != MH_OK && status != MH_ERROR_ENABLED) return false;
    }
    done = true;
    return true;
}
void Capture(bool value) { capture = value; }
ReadPhone::ReadPhone() { ++readers; }
ReadPhone::~ReadPhone() { --readers; }
}  // namespace coop::trade::input
