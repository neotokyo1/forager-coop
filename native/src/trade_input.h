#pragma once

namespace coop::trade::input {
// Installed lazily when the builtin table exists, on the game thread.
bool Install();
void Capture(bool capture);
struct ReadPhone {
    ReadPhone();
    ~ReadPhone();
};
}  // namespace coop::trade::input
