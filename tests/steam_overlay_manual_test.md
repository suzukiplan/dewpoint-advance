# Steam overlay regression checks

Run a build containing this fix from Steam with the in-game overlay enabled.
These checks require a real Steam client and graphics device; the unit tests
cannot validate the injected overlay.

## Cause

Both runtime loops previously slept and continued before presentation whenever
the overlay activation callback paused the game. This also stopped the graphics
calls the overlay needs to draw, leaving a frozen game with no visible overlay.
Steam requires continuous frame rendering:
https://partner.steamgames.com/doc/features/overlay

## Checks

Repeat on Windows (Direct3D) and the SDL backends available on macOS/Linux,
in windowed and fullscreen modes.

1. Start gameplay and open the overlay with Shift+Tab. Verify that the overlay
   appears and remains interactive while the game image and audio are paused.
2. Close the overlay. Verify that gameplay and audio resume. Repeat several
   times, including opening the overlay immediately after launch.
3. Pause with the game's pause shortcut, then open the overlay. Verify that it
   still draws and responds. The fix does not change existing pause/resume policy.
4. While the overlay is open, switch to another app and back; minimize and restore
   the game. Verify that drawing recovers and the overlay can be closed.
5. Exit the game while paused. Verify that shutdown completes normally.

For a Windows debugger/fault-injection run, force `renderer.render` to return
false in the paused branch. Verify that the loop exits with code 1 through the
normal audio/renderer cleanup, matching a rendering failure during gameplay.

The paused path must continue processing window events and Steam callbacks and
present the current VRAM, without calling `gba.tick()` or refilling audio. Its
delay must remain in place to avoid busy-spinning when presentation does not wait.
