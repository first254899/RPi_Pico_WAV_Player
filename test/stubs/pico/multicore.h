// Host stub: core1 is a std::thread
#pragma once
#include <thread>
static inline void multicore_reset_core1() {}
static inline void multicore_launch_core1(void (*entry)()) { std::thread(entry).detach(); }
