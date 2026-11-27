// Host stub of the ARM event register (__sev/__wfe) as used between core0 and core1:
// __sev() signals every thread, __wfe() returns at once if any event was signalled since this
// thread's previous __wfe() (latched event, like the hardware), otherwise blocks until the next one.
// There is no timeout on purpose: a lost wakeup hangs and the test's watchdog reports it.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>

struct StubEvent {
    std::mutex m;
    std::condition_variable cv;
    uint64_t gen = 0;
    static StubEvent& get() { static StubEvent e; return e; }
};

inline thread_local uint64_t stub_event_seen = 0;
inline uint64_t stub_wfe_count = 0;  // number of __wfe() calls that actually slept (protected by StubEvent::m)

static inline void __sev()
{
    StubEvent& e = StubEvent::get();
    { std::lock_guard<std::mutex> lk(e.m); e.gen++; }
    e.cv.notify_all();
}

static inline void __wfe()
{
    StubEvent& e = StubEvent::get();
    std::unique_lock<std::mutex> lk(e.m);
    if (e.gen == stub_event_seen) {
        stub_wfe_count++;
        e.cv.wait(lk, [&e] { return e.gen != stub_event_seen; });
    }
    stub_event_seen = e.gen;
}
