/*------------------------------------------------------/
/ Copyright (c) 2021, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#include "ReadBuffer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    _size(PlayAudio::RDBUF_SIZE), _left(0), _fillThreshold(PlayAudio::RDBUF_THRESHOLD), _isEof(false)
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
    bindReq_t req = {fp, startPos, eodPos, true};
    // send request
    queue_add_blocking(&bindReqQueue, &req);
    // wait response
    queue_remove_blocking(&bindRespQueue, &req);
    while (!queue_is_full(&secondaryBufferQueue)) {}
    fill();
}

void ReadBuffer::reqUnbind()
{
    bindReq_t req = {nullptr, 0, 0, false};
    // send request
    queue_add_blocking(&bindReqQueue, &req);
    // wait response (core1 has discarded all data in secondaryBufferQueue)
    queue_remove_blocking(&bindRespQueue, &req);
}

void ReadBuffer::fillLoop()
{
    int id = 0;
    FIL* fp;
    size_t eodPos = 0;
    bindReq_t req;
    secondaryBufferItem_t item;

    while (true) {
        // expecting bind request
        queue_remove_blocking(&bindReqQueue, &req);
        if (req.flag) {
            fp = req.fp;
            eodPos = req.eodPos;
            _isEod = false;
            id = 0;
            item.pos = req.startPos;
            item.reachedEof = false;
            if (f_lseek(fp, req.startPos) != FR_OK || req.startPos >= eodPos) {
                // nothing to read: zero-length EOF item
                _isEod = true;
                item.ptr = &secondaryBuffer[0];
                item.length = 0;
                item.reachedEof = true;
                queue_try_add(&secondaryBufferQueue, &item);
            }
        } else {
            // discard all data in secondaryBufferQueue (unbind while not streaming)
            while (queue_try_remove(&secondaryBufferQueue, &item)) {}
        }
        queue_try_add(&bindRespQueue, &req);  // response regardless of flag
        if (!req.flag) { continue; }  // retry if unbind

        while (true) {
            // read from file to store secondaryBuffer
            while (!item.reachedEof && !queue_is_full(&secondaryBufferQueue)) {
                // read at once for max efficiency as min of either till the end of buffer or spare number of queue
                int level = static_cast<int>(queue_get_level(&secondaryBufferQueue));
                int reqN = NUM_SECONDARY_BUFFERS - ((id > level) ? id : level);
                // never read at or beyond eodPos (item.pos < eodPos is kept here)
                size_t remain = eodPos - item.pos;
                UINT reqBr;
                if (SECONDARY_BUFFER_SIZE * reqN >= remain) {
                    reqBr = remain;
                    _isEod = true;
                } else {
                    reqBr = SECONDARY_BUFFER_SIZE * reqN;
                }
                UINT br;
                FRESULT fr = f_read(fp, &secondaryBuffer[SECONDARY_BUFFER_SIZE * id], reqBr, &br);
                if (f_eof(fp)) { _isEod = true; }
                if (fr != FR_OK || br == 0) { return; }
                // put on queue divided by SECONDARY_BUFFER_SIZE
                int readN = (br + SECONDARY_BUFFER_SIZE - 1) / SECONDARY_BUFFER_SIZE;
                for (int i = 0; i < readN; i++) {
                    if (i < readN - 1) {
                        item.length = SECONDARY_BUFFER_SIZE;
                        item.reachedEof = false;
                    } else {
                        item.length = br - SECONDARY_BUFFER_SIZE * i;
                        item.reachedEof = _isEod;
                    }
                    item.ptr = &secondaryBuffer[SECONDARY_BUFFER_SIZE * id];
                    item.pos += item.length;
                    queue_try_add(&secondaryBufferQueue, &item);
                    id = (id + 1) % NUM_SECONDARY_BUFFERS;
                }
            }
            // acceptance of unbind
            if (queue_try_remove(&bindReqQueue, &req)) {
                if (!req.flag) {
                    // discard all data in secondaryBufferQueue
                    while (queue_try_remove(&secondaryBufferQueue, &item)) {}
                }
                queue_try_add(&bindRespQueue, &req);  // response regardless of flag
                if (!req.flag) { break; }  // start over if unbind, otherwise ignore
            }
        }
    }
}
