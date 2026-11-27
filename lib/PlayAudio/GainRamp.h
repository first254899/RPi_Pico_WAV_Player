/*------------------------------------------------------/
/ Copyright (c) 2026, first254899
/ Released under the BSD-2-Clause
/ refer to https://opensource.org/licenses/BSD-2-Clause
/------------------------------------------------------*/

#pragma once

#include <cstdint>

//=================================
// GainRamp: linear gain ramp in Q16 (65536 = unity) applied to S32 samples
//=================================
// Math stays in the 32-bit sample domain: sample * gain / 65536 with a 64-bit product, the same
// operation the volume control always used, so a constant gain of 65536 gives the input bit for bit.
class GainRamp
{
public:
    static constexpr int32_t UNITY = 65536;
    explicit GainRamp(int32_t g = 0) { reset(g); }
    // jump to g (no ramp)
    void reset(int32_t g) { cur = g; tgt = g; start = g; remain = 0; len = 0; q = 0; r = 0; err = 0; mag = 0; neg = false; }
    // ramp linearly from the current gain to t over 'frames' frames; frame k (1..frames) gets
    // start + (t - start) * k / frames (rounded toward start), so the last one is exactly t.
    // may be called during a ramp (continues from the current gain). frames == 0 jumps to t.
    // gains are expected in [0, UNITY]
    void setTarget(int32_t t, uint32_t frames)
    {
        tgt = t;
        if (frames == 0 || cur == t) { cur = t; remain = 0; return; }
        start = cur;
        neg = t < cur;
        uint32_t diff = static_cast<uint32_t>(neg ? cur - t : t - cur);
        len = frames;
        remain = frames;
        q = diff / frames;  // one (32-bit) division per ramp
        r = diff % frames;
        err = 0;
        mag = 0;
    }
    int32_t target() const { return tgt; }
    int32_t gain() const { return cur; }
    bool active() const { return remain != 0; }
    uint32_t remaining() const { return remain; }
    // gain for the next frame
    int32_t next()
    {
        if (remain != 0) {
            if (--remain == 0) {
                cur = tgt;
            } else {
                // exact integer interpolation (Bresenham), no division per frame
                mag += q;
                err += r;
                if (err >= len) { err -= len; mag++; }
                cur = neg ? start - static_cast<int32_t>(mag) : start + static_cast<int32_t>(mag);
            }
        }
        return cur;
    }
    static int32_t apply(int32_t sample, int32_t gain)
    {
        return static_cast<int32_t>(static_cast<int64_t>(sample) * gain / 65536);
    }
private:
    int32_t cur;
    int32_t tgt;
    int32_t start;
    uint32_t remain;
    uint32_t len;
    uint32_t q;
    uint32_t r;
    uint32_t err;
    uint32_t mag;   // |gain - start|
    bool neg;
};

//=================================
// Fader: when the decoder fades in/out (PlayWav::decode(), I2S DMA ISR only)
//=================================
// - fade in at play start, unpause, recovery from an underrun, volume changes ramp too
// - fade out on pause, stop, entry into an underrun, using the data that is already buffered
//   (consumes at most rampFrames frames; a shorter fade if less data is buffered)
// - fade out at the end of the track so that the last frame gets gain 0 (never reads past the end);
//   the end of the track is always consumed (played at gain 0 if the fade had to finish early)
// Usage per output buffer:
//   if (!fader.begin(...)) -> output silence, consume nothing
//   for (n = 0; n < avail && fader.next(n, g); n++) -> output frame n with gain g, consume it
//   if (fader.padToFull()) -> fill the rest of the buffer with silence
class Fader
{
public:
    // new track: start from silence
    void restart() { ramp.reset(0); endFade = false; fadeOut = false; playOut = false; }
    // active: playing, not paused, no underrun; gain: volume gain (Q16)
    // avail: frames that can be consumed now (at most one output buffer)
    // eof: all frames left in the track are buffered, framesToEnd: their number (>= avail)
    // returns false if the output is silence and nothing should be consumed
    bool begin(bool active, int32_t gain, uint32_t avail, bool eof, uint32_t framesToEnd, uint32_t rampFrames)
    {
        this->eof = eof;
        this->framesToEnd = framesToEnd;
        this->rampFrames = rampFrames;
        fadeOut = !active || endFade;
        const int32_t target = fadeOut ? 0 : gain;
        if (target != ramp.target()) { ramp.setTarget(target, rampFrames); }
        if (fadeOut && ramp.remaining() > avail) { ramp.setTarget(0, avail); }  // only 'avail' frames left to fade with
        playOut = endFade && active;  // the end of the track is always played out (at gain 0 once faded)
        if (playOut) { return avail > 0; }
        if (fadeOut && !ramp.active()) { return false; }  // silent: consume nothing
        return true;
    }
    // gain g for frame n of this buffer; false: faded out, frame n and later are not consumed
    bool next(uint32_t n, int32_t& g)
    {
        if (fadeOut) {
            if (!ramp.active() && !playOut) { return false; }
        } else if (eof && !endFade && framesToEnd - n <= rampFrames) {
            endFade = true;
            ramp.setTarget(0, framesToEnd - n);  // reaches 0 at the last frame of the track
        }
        g = ramp.next();
        return true;
    }
    bool padToFull() const { return fadeOut; }  // fading out / silent: output a full buffer
    bool silent() const { return ramp.gain() == 0 && !ramp.active(); }
    const GainRamp& getRamp() const { return ramp; }
private:
    GainRamp ramp;
    bool endFade = false;
    bool fadeOut = false;
    bool playOut = false;
    bool eof = false;
    uint32_t framesToEnd = 0;
    uint32_t rampFrames = 0;
};
