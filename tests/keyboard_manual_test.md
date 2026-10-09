# In-game keyboard configuration: platform checks

Run with an updated GBA SDK and runtime on Windows, macOS and Linux.
The automated keymap and bridge tests cover persistence and error handling;
these checks exercise native input and the game frame loop.

1. Query every `DpaButtonId`: the reported keys should match gameplay and the
   loaded INI. With no key held, input must return 0.
2. Hold an unmapped letter, arrow, punctuation key available without modifiers,
   or either Shift key. Input must report its SDK code while held, then 0 on
   release. With multiple keys held, the lowest SDK code wins.
3. Open key capture with a mapped key, wait for release, and assign A to Q.
   Check the setter returns 0, the getter and `dpa_button_a()` return Q, and Q
   operates A immediately. The previous A key must stop operating A.
4. Reassign while the previous key is held; release it. No button may remain
   stuck. Check another simultaneously held button retains its state.
5. Assign both A and B to Q, clear A with code 0, then assign RAPID_A to W.
   Verify both-button input, clearing, and rapid fire respectively.
6. Restart: all assignments and cleared bindings must persist.
7. Switch focus away from the game: keyboard input must return 0. Return focus
   and verify release/repress behavior. Change keyboard layout and verify that
   getter values and A/B labels follow any runtime fallback.
8. Use an installation directory where saving is prohibited. Attempt a change:
   setter must return -1, gameplay/getter/INI must retain the old configuration,
   and the runtime log must explain the failed save.

9. Configure A and RAPID_A to a key unavailable in a second keyboard layout.
   Switch to that layout: A should use its default and RAPID_A should be disabled.
   Change and save B, then inspect the INI: A and RAPID_A must retain their original
   assignments. Return to the original layout without restarting; both assignments,
   getter values and A's label must recover. Repeat on Windows and SDL.
