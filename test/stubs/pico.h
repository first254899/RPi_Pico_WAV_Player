// Host stub: get_core_num() is 1 on the thread started by multicore_launch_core1() (core1), 0 elsewhere
#pragma once
#ifdef __cplusplus
inline thread_local unsigned stub_core_num = 0;
static inline unsigned get_core_num() { return stub_core_num; }
#else
static inline unsigned get_core_num(void) { return 0; }
#endif
