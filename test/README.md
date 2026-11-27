# Host-side unit tests

Runs on the development PC (no Pico SDK, no hardware).

```
cmake -S test -B build-test
cmake --build build-test
ctest --test-dir build-test --output-on-failure
```

* `test_wav_parser`: RIFF/WAVE header parser (`lib/PlayAudio/WavParser.*`) against synthetic files
  (PCM 16/24/32, WAVE_FORMAT_EXTENSIBLE PCM/float, IEEE float, odd-sized and large chunks before `data`,
  `data` size 0 / 0xFFFFFFFF, wrapping chunk sizes, truncated and broken headers, I/O errors)
  and the float to S32 conversion used by the decoder.
* `test_read_buffer`: core0/core1 streaming protocol of `lib/PlayAudio/ReadBuffer.*`, built unmodified against
  `test/stubs` (pico queue, multicore as `std::thread`, FatFs subset with fault injection).
  Short files, data ending before EOF, resume near the end, read/seek errors, random stop/restart, seek, slow card.
  Core1 must sleep (`__wfe`) while idle: the stub's `__sev`/`__wfe` emulate the event register without timeout,
  so a lost wakeup hangs, and idle CPU time is measured.
  A watchdog turns a hang into a failure.

* `test_lcd_text`: UTF-8 file names and tags to LCD font text (`lib/LcdElementBox/lcd_text.*`): Thai with
  vowel/tone marks, CJK, emoji, combining/zero-width characters, invalid UTF-8, CP932 bytes, bounded output, fuzz.

* `test_gain_ramp`: fades (`lib/PlayAudio/GainRamp.h`): ramp math (exact length/end value, monotonic, linear,
  unity bit-exact) and a model of `PlayWav::decode()` buffer by buffer: play start, pause/unpause, stop, track end
  (gain 0 on the last frame, no read past the end), underrun entry/recovery, volume changes, random sequences.

* `test_tag_read`: `src/TagRead.*` against the FatFs stub: ID3v2.2/2.3/2.4 tags, every error exit of the frame
  parser (short header/data reads, 0xFF frame id, unsupported flags), RIFF `id3 ` and `LIST INFO` chunks, a WAV
  shorter than an ID3v1 block, 200 reloads. LeakSanitizer fails the run on any leak.

All are built with AddressSanitizer and UndefinedBehaviorSanitizer.
