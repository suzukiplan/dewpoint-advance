# Change Log

## Version 1.4.1

- Skipped redundant gameplay presentation when no emulation frame advances on Windows and SDL runtimes, with an idle wait to avoid busy-spinning and continuous presentation preserved while paused for the Steam overlay

## Version 1.4.0

- Added `dpa_keyboard_input`, `dpa_keyboard_set`, and `dpa_keyboard_get` for in-game keyboard configuration, with portable SDK key codes and transactional `keymap.ini` updates on Windows and SDL runtimes
- Fixed the Steam overlay failing to appear while the game was paused by continuing to present the last game frame in both the Windows and SDL runtimes
- Added `dpa_sound_volume_dmg` and `dpa_sound_volume_pcm` Bridge APIs to independently adjust emulator-side DMG and PCM volume from 0 (mute) to 100 (maximum, default), with settings persisted in `config.dat`
- Added `dpa_sound_volume_dmg_get` and `dpa_sound_volume_pcm_get` Bridge APIs to retrieve the current volume settings
- Introduced a versioned `config.dat` format with a 4-byte signature, a zero byte, a version byte, and a 2-byte little-endian size; legacy files are automatically migrated while preserving window settings and defaulting both volumes to 100
- Improved resilience to repeated audio underruns during recording or presentation stalls on Windows and SDL runtimes by increasing the audio prebuffer in 50 ms steps up to 200 ms after underruns, while retaining the 50 ms startup default

## Version 1.3.0

- Added support for Steamworks SDK v1.65
- Removed the `com.apple.quarantine` attribute from the copied `libsteam_api.dylib` during macOS test builds

## Version 1.2.0

- Corrected the log.txt record format: `YYYY.MM.DD hh:mm:ss Log record`
- Supported the key mapping function (keymap.ini)

## Version 1.1.0

- Fixed the window aspect ratio to 24:16
- Set the minimum window size to 240×160
- Added keyboard shortcuts 1/2/3/4 to switch the window size to:
  - 1: 240×160 (1×)
  - 2: 480×320 (2×)
  - 3: 720×480 (3×)
  - 4: 960×640 (4×)
- CRT Filter Mode: `-f crt`
- LCD Filter Mode: `-f lcd`
- Improved low-latency audio buffering and underrun recovery on Windows, macOS, and Linux
- Paused emulation while the Steam overlay is open and resumed it when the overlay closes
- Changed sound analog emulation: REAL -> Subtle
- Supported the Vulkan renderer (Linux)
- Supported the Metal renderer (macOS)
