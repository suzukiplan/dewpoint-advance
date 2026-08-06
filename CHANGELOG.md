# Change Log

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
