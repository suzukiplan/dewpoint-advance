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

## Windows audio polling timer resolution

Windows requests 1 ms timer resolution with `timeBeginPeriod(1)` before runtime
initialization. A noncopyable scope guard releases a successful request with
`timeEndPeriod(1)` on normal exit and early returns. Failure to request the
resolution is logged and stops startup instead of silently using coarse waits.
`winmm.lib` is already linked by the Windows build.

On Windows, verify with API breakpoints/fault injection:

1. Normal startup/shutdown: one successful begin call and one matching end call.
2. Force a later startup failure or a gameplay render error: the matching end
   call must still occur.
3. Force `timeBeginPeriod` to fail: startup reports an error and returns 1,
   without calling `timeEndPeriod` for the failed request.
4. Force `timeEndPeriod` to fail: the failure must appear in log.txt.
5. At 60/120/144/240 Hz, compare audio polling intervals and visible/recorded
   frame pacing. Check the idle branch with recording enabled and disabled.

A 1 ms resolution request does not guarantee an exact 1 ms wakeup under load.
Windows 11 may stop honoring higher resolution when an app is fully occluded,
minimized or otherwise invisible/inaudible; include minimize/restore in checks.
Reference: https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod

## Opt-in frame diagnostics

1. Launch with `-v` on Windows, OpenGL, Metal and Vulkan. Check the small DIAG
   panel at the top right of the game image, including with CRT/LCD filters,
   pause/overlay, resize and fullscreen changes. Without `-v`, neither the
   panel nor `[diag]` frame records should appear.
2. Compare identical scenes with recording off/on. Verify `frames` is 0 while
   paused and can exceed 1 during catch-up. Present/texture/emulation timings
   should be nonnegative; no-draw iterations should not produce records.
3. Exit before one second and confirm the partial summary is logged. Normal
   operation should produce one summary per second; repeated hitches must stay
   within four lines per window, with all hitches counted. API failures should
   increase `not_submitted` and retain existing error handling. Check the worst
   candidate and available neighbours; individual draw sequence numbers need
   not be contiguous across windows.
4. Check `game -v`, `game -v -f no`, and `game path/to/rom.gba -v` option parsing.
   Repeat with no `-v` to confirm existing ROM/save/filter options still work.
5. Interpret durations as CPU call times: do not compare asynchronous Metal or
   Vulkan submission time directly against GPU execution time. `render_ms`
   includes `texture_ms`, `present_ms` and `wait_ms` rather than adding to them.

The portable `frame_diagnostics_test` uses a deterministic clock to verify
summary timing, bounded hitch context, counters/maxima, final flush, failures,
paused records, disabled scopes and marker isolation from
VRAM, including null/undersized image inputs. Platform rendering and recording
checks above still require their native devices.

### Diagnostic LAG marker

With `-v`, inject presentation delays above 20 ms and verify a red LAG panel at
the bottom right of the game image on the next draw. Restore intervals to 20 ms
or less and verify it clears on the following draw. Repeat on Windows, OpenGL,
Metal and Vulkan, including filters and fullscreen. Confirm pause hides LAG,
resume/first presentation after device unavailability does not count the gap,
and without `-v` neither marker appears. The mark uses CPU presentation-completion
intervals; it cannot update the screen while the rendering thread is blocked.
The deterministic unit test covers the exact 50 fps boundary, recovery, pause,
failed submissions and display-copy isolation.

LAG state changes must produce exactly one `[diag] LAG ON` or `LAG OFF` record
when the marked/unmarked image is successfully submitted. Verify no repeated
records during sustained lag or normal pacing, no initial OFF record, and no
transition for a failed presentation. Pausing while LAG is visible should log
OFF on the first successful paused draw. Transition records are immediate and
are additional to the four-line summary/context limit.
