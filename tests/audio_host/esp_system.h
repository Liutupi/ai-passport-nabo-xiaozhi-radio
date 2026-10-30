#pragma once
#include <cstdlib>
inline void esp_system_abort(const char *) { std::abort(); }
