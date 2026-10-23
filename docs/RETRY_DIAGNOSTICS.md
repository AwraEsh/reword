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
coverage are useful future extensions. Installed Preview is unchanged.
