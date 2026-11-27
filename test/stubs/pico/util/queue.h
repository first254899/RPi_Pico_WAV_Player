// Host stub of pico_util queue with the same semantics (fixed element size, bounded, copy in/out)
#pragma once
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

typedef struct {
    std::mutex m;
    std::condition_variable cv;
    std::vector<uint8_t> data;
    unsigned elementSize = 0;
    unsigned capacity = 0;
    unsigned head = 0;
    unsigned count = 0;
} queue_t;

static inline void queue_init(queue_t* q, unsigned elementSize, unsigned count)
{
    std::lock_guard<std::mutex> lk(q->m);
    q->data.assign(static_cast<size_t>(elementSize) * count, 0);
    q->elementSize = elementSize; q->capacity = count; q->head = 0; q->count = 0;
}
static inline unsigned queue_get_level(queue_t* q) { std::lock_guard<std::mutex> lk(q->m); return q->count; }
static inline bool queue_is_empty(queue_t* q) { return queue_get_level(q) == 0; }
static inline bool queue_is_full(queue_t* q) { std::lock_guard<std::mutex> lk(q->m); return q->count == q->capacity; }

static inline bool queue_add_internal(queue_t* q, const void* e, bool block)
{
    std::unique_lock<std::mutex> lk(q->m);
    if (q->count == q->capacity) {
        if (!block) { return false; }
        q->cv.wait(lk, [q] { return q->count < q->capacity; });
    }
    memcpy(&q->data[((q->head + q->count) % q->capacity) * q->elementSize], e, q->elementSize);
    q->count++;
    q->cv.notify_all();
    return true;
}
static inline bool queue_remove_internal(queue_t* q, void* e, bool block, bool remove)
{
    std::unique_lock<std::mutex> lk(q->m);
    if (q->count == 0) {
        if (!block) { return false; }
        q->cv.wait(lk, [q] { return q->count > 0; });
    }
    if (e) { memcpy(e, &q->data[q->head * q->elementSize], q->elementSize); }
    if (remove) {
        q->head = (q->head + 1) % q->capacity;
        q->count--;
        q->cv.notify_all();
    }
    return true;
}
static inline bool queue_try_add(queue_t* q, const void* e) { return queue_add_internal(q, e, false); }
static inline void queue_add_blocking(queue_t* q, const void* e) { queue_add_internal(q, e, true); }
static inline bool queue_try_remove(queue_t* q, void* e) { return queue_remove_internal(q, e, false, true); }
static inline void queue_remove_blocking(queue_t* q, void* e) { queue_remove_internal(q, e, true, true); }
static inline void queue_peek_blocking(queue_t* q, void* e) { queue_remove_internal(q, e, true, false); }
