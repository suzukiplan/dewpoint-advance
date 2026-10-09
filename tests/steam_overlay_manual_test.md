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

## Recording stalls and audio recovery

The SDL and DirectSound loops both refill PCM before a synchronous presentation.
A presentation/capture stall can consume their fixed 50 ms prebuffer; restarting
at that same target makes recurring stalls repeatedly interrupt audio and slow
emulation, which is paced by audio consumption. DirectSound can reach its safe
write boundary even before all queued PCM has played.

Recovery now increases the target by 50 ms per detected underrun, capped at
200 ms. The increased target also controls playback restart, and survives pauses
for the lifetime of the runtime. Normal startup retains the original 50 ms
latency. This trades up to 150 ms of additional audio/input buffering for
resilience; it does not make presentation nonblocking or eliminate sustained CPU
or GPU overload. Stalls beyond the cap still cause underruns. The exact Steam
recording report has not been reproduced on a Windows Steam installation here.

1. Compare recording off/on during the same gameplay on Windows and SDL.
   Check frame pacing, music continuity, and timing relative to game events.
2. With a debugger or temporary delay immediately before presentation, inject
   recurring 80 ms stalls. After the first recovery, check that underrun logging
   stops, while emulation catches up across multiple frames per presentation.
3. Repeat with 130 ms stalls and then delays longer than 200 ms. Check that
   recovery works, the target stays bounded, and event handling remains active.
4. Pause/resume and open/close the overlay after recovery. Check that silence
   during an intentional pause does not increase the target and that playback
   resumes after prebuffering. Restart the app to restore the 50 ms baseline.

`audio_prebuffer_test` checks the recovery policy with repeated simulated stalls,
frame alignment, the latency cap, and available DirectSound ring capacity. It
is not an integration test of the audio devices or Steam capture hook.

## Skipping presentation without emulation progress

On Windows and all SDL renderers (OpenGL, Metal, Vulkan), normal gameplay now
presents only after at least one `gba.tick()` in that loop iteration. Iterations
with a full audio queue sleep for 1 ms instead, even when VSync is enabled,
since they no longer reach the presentation wait. Paused iterations continue
presenting for the overlay. This does not compare VRAM: emulated frames still
trigger presentation when the scene is visually static.

1. Compare 60 Hz and available higher refresh rates, with Steam recording off
   and on. Observe presentation and emulation counts separately: idle audio
   refill iterations should produce no render calls; an iteration advancing
   multiple emulation frames should produce one render call.
2. Verify normal game speed, music continuity and rapid fire, including a
   visually static scene. Check that idle iterations do not busy-spin.
3. Repeat the overlay/pause checks above; rendering must continue while paused.
4. Resize, toggle fullscreen, minimize/restore and trigger device recovery.
   During gameplay the next emulation frame must redraw; while paused the
   continuous presentation path must redraw.
5. On Windows, force a render failure after emulation advances and verify
   normal cleanup with exit code 1. Check that an empty audio packet still
   presents the frame that was advanced before the empty packet was detected.

These are device/recording integration checks; they require real Windows and
SDL graphics environments and are not covered by the portable unit tests.
