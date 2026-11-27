/*------------------------------------------------------/
/ Copyright (c) 2021, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#include "ReadBuffer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"

ReadBuffer* ReadBuffer::_inst = nullptr;

void readBufferCore1Process()
{
    flash_safe_execute_core_init();  // no access to flash on core1
    ReadBuffer::getInstance()->fillLoop();
    printf("ERROR: ReadBuffer::fillLoop() exit\r\n");
}

ReadBuffer* ReadBuffer::getInstance()
{
    if (_inst == nullptr) {
        // create Singleton instance
        _inst = new ReadBuffer();
        // start process on core1
        multicore_reset_core1();
        multicore_launch_core1(readBufferCore1Process);
    }
    return _inst;
}

// fillThreshold: auto fill if left is lower than fillThreshold
//                set fillThreshold = 0 if using manual fill instead of auto fill
//                set fillThreshold = size if auto fill everytime when shift (not recommended due to too many memmove)
ReadBuffer::ReadBuffer() :
    _size(PlayAudio::RDBUF_SIZE), _left(0), _fillThreshold(PlayAudio::RDBUF_THRESHOLD), _isEof(false), _isError(false)
{
    _head = reinterpret_cast<uint8_t*>(calloc(_size, sizeof(uint8_t)));
    _ptr = _head;
    // initialize queues here (on core0) before core1 is launched, otherwise reqBind() may touch them uninitialized
    queue_init(&bindReqQueue, sizeof(bindReq_t), 1);
    queue_init(&bindRespQueue, sizeof(bindReq_t), 1);
    queue_init(&secondaryBufferQueue, sizeof(secondaryBufferItem_t), NUM_SECONDARY_BUFFERS);
}

ReadBuffer::~ReadBuffer()
{
    free(_head);
}

const uint8_t* ReadBuffer::buf()
{
    return reinterpret_cast<const uint8_t*>(_ptr);
}

bool ReadBuffer::fill()
{
    if (_isEof) { return false; }
    if (queue_is_empty(&secondaryBufferQueue)) {
        // insert zeros to avoid blank noise
        memmove(_head, _ptr, _left);
        _ptr = _head;
        size_t space = _size - _left;
        memset(_head + _left, 0, space);
        _left += space;
        printf("ERROR: ReadBuffer::secondaryBuffer is empty\r\n");
        return false;
    }
    secondaryBufferItem_t item;
    queue_peek_blocking(&secondaryBufferQueue, &item);  // not remove here to preend from overwrite by new data
    memmove(_head, _ptr, _left);
    _ptr = _head;
    size_t space = _size - _left;
    memcpy(_head + _left, item.ptr, item.length);
    queue_remove_blocking(&secondaryBufferQueue, &item);  // remove from queue here actually
    _pos = item.pos;
    _left += item.length;
    _isEof = item.reachedEof;
    _isError = item.error;
    if (space > item.length) {
        memset(_head + _left, 0, space - item.length);  // fill 0
    }
    return true;
}

bool ReadBuffer::shift(size_t bytes)
{
    if (_left < bytes) { return false; }
    _ptr += bytes;
    _left -= bytes;
    if (_left < _fillThreshold) { fill(); }
    return true;
}

bool ReadBuffer::shiftAll()
{
    return shift(_left);
}

// rebind from pos, must not be called while decode() consumes the buffer
bool ReadBuffer::seek(size_t pos)
{
    if (pos >= _eodPos) { return false; }
    reqUnbind();  // disconnect secondaryBuffer (dispose current secondaryBuffer)
    reqBind(_fp, pos, _eodPos);  // reconnect from new position
    return true;
}

size_t ReadBuffer::getLeft()
{
    return _left;
}

size_t ReadBuffer::tell()
{
    return _pos - _left;
}

bool ReadBuffer::isFull()
{
    return queue_is_full(&secondaryBufferQueue);
}

// all data till the end of stream (or till a read error) has been moved to buf()
bool ReadBuffer::isEof()
{
    return _isEof;
}

// stream was terminated by a read error
bool ReadBuffer::isError()
{
    return _isError;
}

// enough data is buffered: queue is full or all data till the end has been read
bool ReadBuffer::isFilled()
{
    return _isEod || queue_is_full(&secondaryBufferQueue);
}

bool ReadBuffer::isNearEmpty()
{
    return (!_isEod && queue_get_level(&secondaryBufferQueue) <= NUM_SECONDARY_BUFFERS / 4);
}

void ReadBuffer::reqBind(FIL* fp, size_t startPos, size_t eodPos)
{
    // reset core0 side (core1 doesn't touch these)
    _fp = fp;
    _ptr = _head;
    _left = 0;
    _pos = startPos;
    _eodPos = eodPos;
    _isEof = false;
    _isError = false;
    bindReq_t req = {fp, startPos, eodPos, true};
    // send request
    queue_add_blocking(&bindReqQueue, &req);
    // wait response, which core1 sends when initial fill is done
    // (secondaryBufferQueue is full or holds everything till the end of data)
    queue_remove_blocking(&bindRespQueue, &req);
    fill();
}

void ReadBuffer::reqUnbind()
{
    bindReq_t req = {nullptr, 0, 0, false};
    // send request
    queue_add_blocking(&bindReqQueue, &req);
    // wait response (core1 stopped reading and discarded all data in secondaryBufferQueue)
    queue_remove_blocking(&bindRespQueue, &req);
}

// core1: serve bind/unbind requests forever
void ReadBuffer::fillLoop()
{
    bindReq_t req;
    secondaryBufferItem_t item;
    queue_remove_blocking(&bindReqQueue, &req);
    while (true) {
        // discard all data of previous binding (core0 doesn't consume it while waiting for the response)
        while (queue_try_remove(&secondaryBufferQueue, &item)) {}
        if (req.flag) {
            stream(req);  // returns with next request in req
        } else {
            queue_add_blocking(&bindRespQueue, &req);  // response to unbind
            queue_remove_blocking(&bindReqQueue, &req);
        }
    }
}

// core1: stream [req.startPos, req.eodPos) of req.fp into secondaryBufferQueue
// return when next request arrives, which is stored in req
// a read error terminates the stream with an error item but core1 keeps serving requests
void ReadBuffer::stream(bindReq_t& req)
{
    FIL* const fp = req.fp;
    const size_t eodPos = req.eodPos;
    size_t pos = req.startPos;
    int id = 0;
    bool done = false;   // reached eodPos, no more read
    bool ready = false;  // response to bind has been sent
    secondaryBufferItem_t item;

    _isEod = false;
    bool seekError = (pos < eodPos && f_lseek(fp, pos) != FR_OK);
    if (pos >= eodPos || seekError) {
        // nothing to read: zero-length EOF item
        if (seekError) { printf("ERROR: ReadBuffer f_lseek failed\r\n"); }
        done = true;
        item = {&secondaryBuffer[0], pos, 0, true, seekError};
        queue_try_add(&secondaryBufferQueue, &item);
        _isEod = true;
    }

    while (true) {
        // acceptance of next request (only after the response to bind, otherwise it's not expected)
        if (ready && queue_try_remove(&bindReqQueue, &req)) { return; }

        // read from file to store secondaryBuffer
        if (!done && !queue_is_full(&secondaryBufferQueue)) {
            // read at once for max efficiency as min of either till the end of buffer or spare number of queue
            // slots [id, id + reqN) are free because the queue holds the 'level' slots just before id
            int level = static_cast<int>(queue_get_level(&secondaryBufferQueue));
            int reqN = NUM_SECONDARY_BUFFERS - ((id > level) ? id : level);
            // never read at or beyond eodPos (pos < eodPos is kept while !done)
            size_t remain = eodPos - pos;
            size_t reqBr = SECONDARY_BUFFER_SIZE * reqN;
            if (reqBr >= remain) {
                reqBr = remain;
                done = true;
            }
            UINT br;
            FRESULT fr = f_read(fp, &secondaryBuffer[SECONDARY_BUFFER_SIZE * id], static_cast<UINT>(reqBr), &br);
            // br < reqBr: file is shorter than expected. data read before an error is still valid
            bool error = (fr != FR_OK || br < reqBr);
            if (error) {
                printf("ERROR: ReadBuffer f_read failed (fr = %d, %d/%d bytes)\r\n", static_cast<int>(fr), static_cast<int>(br), static_cast<int>(reqBr));
                done = true;
            }
            pos += br;
            // put on queue divided by SECONDARY_BUFFER_SIZE (at least one item to carry EOF)
            int readN = (br + SECONDARY_BUFFER_SIZE - 1) / SECONDARY_BUFFER_SIZE;
            if (readN == 0) { readN = 1; }
            size_t itemPos = pos - br;
            for (int i = 0; i < readN; i++) {
                item.ptr = &secondaryBuffer[SECONDARY_BUFFER_SIZE * id];
                item.length = (i < readN - 1) ? SECONDARY_BUFFER_SIZE : br - SECONDARY_BUFFER_SIZE * i;  // 0 if br == 0
                itemPos += item.length;
                item.pos = itemPos;
                item.reachedEof = done && (i == readN - 1);
                item.error = error && item.reachedEof;
                queue_try_add(&secondaryBufferQueue, &item);  // never fails: readN <= reqN free slots
                id = (id + 1) % NUM_SECONDARY_BUFFERS;
            }
            // only after queueing: isNearEmpty() must not report false while the last items are not in the queue yet
            if (done) { _isEod = true; }
        }

        // response to bind when initial fill is done
        if (!ready && (done || queue_is_full(&secondaryBufferQueue))) {
            queue_add_blocking(&bindRespQueue, &req);
            ready = true;
        }

        // nothing to do until core0 takes an item from secondaryBufferQueue (fill()) or sends the next request.
        // Both are queue operations, which end with __sev() on core0, so sleep instead of spinning.
        // No lost wakeup: an event sent after the checks below and before __wfe() is latched and __wfe() returns at once.
        // Spurious wakeups only cost one more pass of the loop.
        if (ready && (done || queue_is_full(&secondaryBufferQueue)) && queue_is_empty(&bindReqQueue)) {
            __wfe();
        }
    }
}
