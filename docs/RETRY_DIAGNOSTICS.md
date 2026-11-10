# Retry observation investigation — 2026-10-23

The owned X11 workload reproduced `native-text-mismatch` in `live-2` after
95.006 seconds. Numeric events show first correction at monotonic 3218239 ms,
then reset reason 7 at 3218299 ms, then a new first correction at 3218510 ms.
Reason 7 means unchanged observed text with a changed caret. This locates the
reset; it does not establish an atomic snapshot of the GTK field.

Text and caret are separate native reads. A deterministic Service regression
models deletion completing between them. Before the fix, the second attempt
becomes `the ` instead of the expected `teh `. A separate control confirms a
real, persistent caret movement resets the cycle.

The change confirms a caret-only observation once through `permitted(&f)`.
This repeats target identity and privacy checks. A newly observed deletion is
processed normally; a confirmed caret movement still resets retry state. An
unsafe or unavailable confirmation clears observation. No write is retried.

## Diagnostic contract

`REWORD_RETRY_DIAGNOSTICS` enables fixed numeric stderr events only. It is off
by default and enabled by the owned test runner. The runner discards all other
stderr, keeps at most 64 parsed events and bounds each read to 4096 bytes.
No original/corrected text, executable identity or window title is included.

Format: `RTRY <monotonic milliseconds> <reason> <word start offset>`.

| Reason | Meaning |
| --- | --- |
| 1 | Retry decision: correct |
| 2 | Retry decision: preserve |
| 3 | Other observation reset, including actions/shutdown |
| 4 | Pending shortcut crossed safety/identity boundary |
| 5 | Field unavailable or not permitted |
| 6 | Field identity changed |
| 7 | Confirmed same-text caret change |
| 8 | Prefix, location or continued-text boundary |
| 9 | Different valid token at retry location |
| 10 | Post-write field identity unavailable/changed |
| 11 | Post-write contents differ |
| 12 | Post-write caret differs |
| 13 | Failed snapshot, then same permitted metadata except missing focus |
| 14 | Failed snapshot, then same permitted metadata with focus restored |
| 15 | Undo requested with no saved original |
| 16 | Policy rejected the live write before calling the adapter |
| 17 | Native replacement returned false |
| 18 | Undo shortcut reached the service |
| 19 | Current policy rejected Undo before contacting the adapter |
| 20 | The adapter did not acknowledge the Undo write |
| 21 | The write was acknowledged but the field or policy could not confirm Undo |
| 22 | Undo was confirmed |
| 23 | Selected correction shortcut reached the service |
| 24 | Selection unavailable or outside the current policy |
| 25 | Selected correction stopped before a write, due to policy, deadline or worker result |
| 26 | Selected correction could not revalidate the original target |
| 27 | Native provider did not acknowledge the selected replacement |
| 28 | Selected replacement acknowledged, with confirmation or policy mismatch |
| 29 | Selected replacement confirmed and history handling completed |
| 30 | Selected job could not start, including a conflicting active operation |
| 31 | Selected correction hit an exception |
| 32 | Selected job submitted to the worker |

Reasons 13/14 describe a later metadata observation, not proof of the original
failure's cause. They never authorize text access or replacement.

Earlier diagnostic run stopped in `live-undo` at 47.864 seconds after a
post-write reset (then undifferentiated reason 10). That separate failure is
not yet resolved. Another run stopped at the unchanged CPU budget after
68.549 seconds. None of these runs is stability acceptance. Ignored local
receipts are `.deps/retry-diagnostic-active-120`,
`.deps/retry-reasons-active-120` and `.deps/retry-reasons-repeat-active-120`.

After the confirmation change, `.deps/retry-confirm-active-120` completes 12
first/second/third/Undo sequences, 121 selected corrections and ten full cycles
without a text mismatch. It stops at the unchanged CPU gate at 68.993 seconds:
app 10.070% over a 32.17-second baseline, helper 5.197%, zero swap, app private
growth 340 KiB, clean shutdown. This is not a completed duration pass.

Build succeeds. Full CTest passes 14/15 targets in 115.90 seconds; Service
includes both new regressions. `fcitx_state` fails the paused timer witness
(`witnessTicks` 0 after 211 ms); a focused private-D-Bus rerun passes all five
timer cases. Keep the original failure visible. Independent static review finds
no actionable defect; confirmation-specific safety and third-attempt race
coverage were suggested. Follow-up coverage now verifies third-attempt behavior
in the split-read sequence and rejects password/unknown/changed-target metadata
before any confirmation text read. It asserts no extra write or history and
cleared transient buffers. The bounded rebuild succeeds and all 15 CTest
targets pass in 124.18 seconds. The earlier timer failure remains part of the
record. Installed Preview is unchanged.

## Post-write deletion boundary

The expanded diagnostic reproduces another live-2 mismatch at 42.976 seconds
in `.deps/undo-diagnostic-180`: first correction 4834310 ms, post-write text
differs at 4834366 ms (reason 11), unexpected first correction 4834578 ms.
CPU had not exceeded its budget before this failure. A separate run stopped
at `selected-undo` after 61.339 seconds, after its correction/history insert
had succeeded. Its exact rejection was not yet instrumented; do not attribute
it to a particular focus boundary.

A deterministic deletion-after-write regression fails before the change:
second attempt becomes `the ` instead of `teh `. An unrelated-edit control
passes before the change. The new branch preserves the retry decision only
for a freshly permitted, unchanged field/policy, no selection, a caret within
the corrected word interval and text exactly matching deletion from the new
caret through the previous correction caret. It clears stale Undo, observes
the actual newer text, and performs no additional write, count or notification.
Other post-write mismatches still clear observation. Full build and all 17
CTest targets pass in 131.64 seconds on 2026-10-29. This does not establish
native retry stability: `.deps/postwrite-deletion-diagnostic-180` still fails
live-2 at 48.293 seconds, with two correct decisions and no reset between them.
The replacement bool conflates refusal before a write with unsuccessful
confirmation after an applied write; explicit native outcomes remain a candidate
next change. Preserve the genuinely rejected-write rollback regression. Reasons
16/17 now distinguish the two live rejection branches. The subsequent
`.deps/write-outcome-diagnostic-180` was cancelled by the user at 138.657 seconds
with its CPU-overrun failure recorded and clean, unforced shutdown.

For rare-error diagnosis only, `REWORD_ACTIVE_DIAGNOSTIC_CONTINUE_CPU=1` lets
the owned runner continue a CPU overrun while recording `resource-budget`
failure and `cpuBudgetExceeded`. `finish()` cannot pass after that failure.
The diagnostic target duration is capped at 300 seconds; memory, swap and
growth overruns remain fatal. Default runs retain the original CPU abort gate.
Four budget tests cover default abort, diagnostic failure retention, memory
and swap limits, and rejection of targets over 300 seconds before setup.
