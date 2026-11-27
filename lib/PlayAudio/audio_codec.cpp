/*------------------------------------------------------/
/ Copyright (c) 2021, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#include "audio_codec.h"

#include <cstdio>

#include "pico/stdlib.h"

#include "PlayNone.h"
#include "PlayWav.h"
#include "ReadBuffer.h"

static PlayAudio* playAudio_ary[2] = {};
static void (*decode_func_ary[2])() = {};
static PlayAudio::audio_codec_t cur_audio_codec = PlayAudio::AUDIO_CODEC_NONE;
static void (*set_dac_enable_func)(bool flag) = nullptr;

void audio_codec_init()
{
    PlayAudio::initialize();
    playAudio_ary[PlayAudio::AUDIO_CODEC_NONE] = static_cast<PlayAudio*>(new PlayNone());
    playAudio_ary[PlayAudio::AUDIO_CODEC_WAV]  = static_cast<PlayAudio*>(new PlayWav());
    decode_func_ary[PlayAudio::AUDIO_CODEC_NONE] = PlayNone::decode_func;
    decode_func_ary[PlayAudio::AUDIO_CODEC_WAV]  = PlayWav::decode_func;
    cur_audio_codec = PlayAudio::AUDIO_CODEC_NONE;
}

void audio_codec_deinit()
{
    PlayAudio::finalize();
    delete playAudio_ary[PlayAudio::AUDIO_CODEC_NONE];
    delete playAudio_ary[PlayAudio::AUDIO_CODEC_WAV];
}

void audio_codec_set_dac_enable_func(void (*func)(bool flag))
{
    set_dac_enable_func = func;
}

void audio_codec_dac_enable(bool flag)
{
    if (set_dac_enable_func != nullptr) {
        (*set_dac_enable_func)(flag);
    }
}

PlayAudio* get_audio_codec()
{
    return playAudio_ary[cur_audio_codec];
}

PlayAudio* set_audio_codec(PlayAudio::audio_codec_t audio_codec)
{
    cur_audio_codec = audio_codec;
    return get_audio_codec();
}

// The I2S DMA IRQ path (decode(), ReadBuffer::fill(), the I2S library) doesn't print, because printf
// in an interrupt handler can block on the stdio mutex or take too long. It only counts events,
// and this function prints the changes. Call it periodically from the main loop.
void audio_codec_log_events()
{
    static uint32_t lastUnderrun = 0;
    static uint32_t lastZeroFill = 0;
    static uint32_t lastI2sDropped = 0;
    uint32_t underrun = PlayAudio::getRdbufUnderrunCount();
    uint32_t zeroFill = ReadBuffer::getZeroFillCount();
    uint32_t i2sDropped = i2s_lib_msgs_dropped();
    if (underrun != lastUnderrun) {
        printf("AUDIO::rdbuf near empty. inserted instant mute (%lu times, total %lu)\r\n",
            static_cast<unsigned long>(underrun - lastUnderrun), static_cast<unsigned long>(underrun));
        lastUnderrun = underrun;
    }
    if (zeroFill != lastZeroFill) {
        printf("ERROR: ReadBuffer::secondaryBuffer was empty, inserted zeros (%lu times, total %lu)\r\n",
            static_cast<unsigned long>(zeroFill - lastZeroFill), static_cast<unsigned long>(zeroFill));
        lastZeroFill = zeroFill;
    }
    if (i2sDropped != lastI2sDropped) {
        printf("I2S library messages suppressed in the interrupt handler (%lu times, total %lu)\r\n",
            static_cast<unsigned long>(i2sDropped - lastI2sDropped), static_cast<unsigned long>(i2sDropped));
        lastI2sDropped = i2sDropped;
    }
}

// callback from:
//   void __isr __time_critical_func(audio_i2s_dma_irq_handler)()
//   defined at pico_audio_i2s_32b/audio_i2s.c
//   where i2s_callback_func() is declared with __attribute__((weak))
void i2s_callback_func()
{
    decode_func_ary[cur_audio_codec]();
}