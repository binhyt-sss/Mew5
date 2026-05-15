# SG2002 Linux Wakeword Port

This is a Linux-ready port of the ESP32-S3 wakeword pipeline for Milk-V Duo 256M (SG2002) big-core Linux.

The first conversion stage keeps the existing MicroWakeWord model and MicroFrontend pipeline, but replaces the ESP-IDF app shell with a standalone Linux CLI that can be tested without INMP441 wired yet.

## What it does now

- Loads the existing `loopy` wakeword model from `audio-client-esp32s3/src/loopy_model_microwakeword.h`.
- Uses the same 16 kHz / 30 ms window / 10 ms step MicroFrontend path.
- Reads a 16-bit PCM WAV file and feeds it in chunks.
- Prints wakeword detections on Linux.

## Build

Cross-compile on the SG2002 toolchain or native Linux toolchain that matches your Buildroot image.

```bash
cmake -S audio-client-sg2002-linux -B build-sg2002
cmake --build build-sg2002 -j
```

If you are cross-compiling for the Duo 256M Buildroot image, set `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER` to the riscv64 musl toolchain from your SDK.

## Run

```bash
./build-sg2002/audio_wakeword_linux --wav sample_16k_mono.wav
```

The WAV file must be 16 kHz PCM 16-bit mono or stereo. Stereo input is downmixed to mono.

## Next step

Once INMP441 is wired, replace the WAV reader with a Linux audio capture backend such as ALSA `snd_pcm_readi()` and keep the same wakeword core.
