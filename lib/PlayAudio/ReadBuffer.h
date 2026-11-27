/*------------------------------------------------------/
/ Copyright (c) 2021, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#pragma once

#include "pico/util/queue.h"

#include "ff.h"
#include "PlayAudio.h"

//=================================
// Interface of ReadBuffer Class
//=================================
class ReadBuffer
{
public:
    static ReadBuffer* getInstance();  // Singleton
    ReadBuffer();
    virtual ~ReadBuffer();
    // core1 streams [startPos, eodPos) of fp, never reading beyond eodPos
    void reqBind(FIL* fp, size_t startPos, size_t eodPos);
    void reqUnbind();
    const uint8_t* buf();
    bool shift(size_t bytes);
    bool shiftAll();
    bool seek(size_t pos);
    size_t getLeft();
    size_t tell();
    bool isFull();
    bool isNearEmpty();
private:
    static constexpr size_t SECONDARY_BUFFER_SIZE = PlayAudio::RDBUF_SIZE - PlayAudio::RDBUF_THRESHOLD;
    static constexpr size_t NUM_SECONDARY_BUFFERS = 8;
    static ReadBuffer* _inst;  // Singleton instance
    uint8_t secondaryBuffer[SECONDARY_BUFFER_SIZE * NUM_SECONDARY_BUFFERS];
    typedef struct _secondaryBufferItem_t {
        uint8_t* ptr;
        size_t   pos;
        size_t   length;
        bool     reachedEof;
    } secondaryBufferItem_t;
    typedef struct _bindReq_t {
        FIL*   fp;
        size_t startPos;  // file position to start reading
        size_t eodPos;    // end of data: core1 never reads at or beyond this position
        bool   flag;      // true: bind, false: unbind
    } bindReq_t;
    queue_t secondaryBufferQueue;
    queue_t bindReqQueue;
    queue_t bindRespQueue;
    FIL* _fp;
    size_t _size;
    size_t _pos;
    size_t _left;
    size_t _eodPos;  // core0 copy of bound eodPos (for seek)
    volatile bool _isEod;  // written by core1, read by core0
    uint8_t* _head;
    uint8_t* _ptr;
    size_t _fillThreshold;
    bool _isEof;
    bool fill();
    void fillLoop();
    friend void readBufferCore1Process();
};
