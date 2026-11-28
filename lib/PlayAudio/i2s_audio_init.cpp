/*------------------------------------------------------/
/ Copyright (c) 2023, Elehobica
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#include "i2s_audio_init.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "pico/stdlib.h"

static audio_buffer_pool_t* _producer_pool = nullptr;
// audio_buffer_t arrays allocated by audio_new_buffer_pool() (pico_audio_32b), which neither
// audio_i2s_end() nor anything else frees: remembered here to free them in i2s_audio_deinit()
static audio_buffer_t* _producer_buffers = nullptr;
static audio_buffer_t* _consumer_buffers = nullptr;
static volatile uint32_t _lib_msgs_dropped = 0;

static audio_format_t audio_format = {
    .sample_freq = 44100,
    .pcm_format = AUDIO_PCM_FORMAT_S32,
    .channel_count = AUDIO_CHANNEL_STEREO
};

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 8
};

static audio_i2s_config_t i2s_config = {
    .data_pin = PICO_AUDIO_I2S_DATA_PIN,
    .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
    .dma_channel0 = 0,
    .dma_channel1 = 1,
    .pio_sm = 0
};

// printf of lib/pico_audio_i2s_32b/src/audio_i2s.c is redirected here (see CMakeLists.txt): its
// update_pio_frequency() prints and now runs in the DMA IRQ when the sample rate changes.
// In an exception handler the message is dropped (counted), stdio there could block or be slow.
extern "C" int i2s_lib_printf(const char* __restrict fmt, ...)
{
    if (__get_current_exception() != 0) {
        _lib_msgs_dropped = _lib_msgs_dropped + 1;
        return 0;
    }
    va_list va;
    va_start(va, fmt);
    int n = vprintf(fmt, va);
    va_end(va);
    return n;
}

uint32_t i2s_lib_msgs_dropped()
{
    return _lib_msgs_dropped;
}

// first call: create the I2S pipeline
// sample rate change: keep pool, buffers, DMA and PIO and only retune the PIO clock divider.
//   pico_audio_i2s_32b supports this ("dynamic frequency shifting" in wrap_consumer_take()): when the
//   producer format's sample_freq differs from the current one, the divider is updated as the next
//   buffer is taken, i.e. in the next DMA IRQ (within one buffer period). Nothing is allocated or freed.
//   The caller keeps the DAC muted meanwhile (PlayAudio::play()).
void i2s_setup(uint32_t samp_freq, audio_buffer_pool_t*& ap)
{
    printf("Samp Freq = %d Hz\n", static_cast<int>(samp_freq));
    if (_producer_pool == nullptr) {
        i2s_audio_init(samp_freq);
    } else {
        audio_format.sample_freq = samp_freq;  // single aligned word: the IRQ sees either value
    }
    ap = _producer_pool;
}

void i2s_audio_init(uint32_t sample_freq)
{
    audio_format.sample_freq = sample_freq;

    _producer_pool = audio_new_producer_pool(&producer_format, 3, SAMPLES_PER_BUFFER);
    _producer_buffers = _producer_pool->free_list;  // right after creation the free list starts at the array

    bool __unused ok;
    const audio_format_t *output_format;

    output_format = audio_i2s_setup(&audio_format, &audio_format, &i2s_config);
    if (!output_format) {
        panic("PicoAudio: Unable to open audio device.\n");
    }

    ok = audio_i2s_connect(_producer_pool);
    assert(ok);
    _consumer_buffers = _producer_pool->connection->consumer_pool->free_list;  // nothing taken before enabling
    { // initial buffer data
        audio_buffer_t* ab = take_audio_buffer(_producer_pool, true);
        int32_t* samples = (int32_t*) ab->buffer->bytes;
        for (uint i = 0; i < ab->max_sample_count; i++) {
            samples[i*2+0] = DAC_ZERO;
            samples[i*2+1] = DAC_ZERO;
        }
        ab->sample_count = ab->max_sample_count;
        give_audio_buffer(_producer_pool, ab);
    }
    audio_i2s_set_enabled(true);
}

void i2s_audio_deinit()
{
    audio_i2s_set_enabled(false);
    audio_i2s_end();

    audio_buffer_t* ab;
    ab = take_audio_buffer(_producer_pool, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = take_audio_buffer(_producer_pool, false);
    }
    ab = get_free_audio_buffer(_producer_pool, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_free_audio_buffer(_producer_pool, false);
    }
    ab = get_full_audio_buffer(_producer_pool, false);
    while (ab != nullptr) {
        free(ab->buffer->bytes);
        free(ab->buffer);
        ab = get_full_audio_buffer(_producer_pool, false);
    }
    free(_producer_buffers);
    _producer_buffers = nullptr;
    free(_producer_pool);
    _producer_pool = nullptr;
    // audio_i2s_end() freed the consumer buffers' memory and the pool, but not the array
    free(_consumer_buffers);
    _consumer_buffers = nullptr;
}
