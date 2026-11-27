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
  A watchdog turns a hang into a failure.

Both are built with AddressSanitizer and UndefinedBehaviorSanitizer.
