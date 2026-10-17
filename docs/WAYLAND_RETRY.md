# Wayland retry reset verification

The approved retry contract preserves alternate attempts only while the user
rewrites the same word at the same location. Moving the cursor, continuing text,
changing fields or waiting more than 30 seconds starts a new cycle.

The private GNOME/Fcitx probe already exercises first/second/third correction and
rejected native writes. Extend it with actual Left/Right input, text continuation
and a real 31-second delay. Run Qt and GTK separately, keeping fixture lifetimes
long enough for the delay. Confirm failures before changing the addon.

If the addon misses a reset, inspect its key and surrounding-text event flow.
Preserve deletion/retyping at the same location, and do not treat asynchronous
intermediate native replacement updates as cursor navigation. Password and
untrusted-field data must not enter retained observation state.

After the minimal fix, run both native probes, the full suite and independent
review. Record exact evidence and remaining compatibility limits here before
integration. The fixtures do not prove every application or physical lock state.

## Reproduced failures and changes

The new Qt probe first failed after actual Left/Right movement: `teh ` remained
on the next attempt instead of starting a fresh correction cycle. After a
navigation-only fix, continuation also failed. The addon had only inspected the
word prefix at delimiter events, missing navigation away and back and text
continued then deleted before the next delimiter.

The addon now watches surrounding-text updates and capability changes. It
compares a SHA256 fingerprint and cursor/anchor positions, preserves asynchronous
replacement confirmation, and resets when the prefix changes or text continues
past a word delimiter. The observer is Wayland-only and checks field permission,
password/sensitive capabilities and session lock before reading surrounding text.
Only a fingerprint of the last observed text is retained for cursor comparison.

The stronger probe also exposed an earlier false-positive Pause test: the
expected uncorrected word could result from the alternating retry cycle even if
the hotkey did nothing. It now checks the persisted paused state explicitly.
Ctrl+Alt+P key releases reached the addon while presses did not; Fcitx consumes
that combination for TogglePreedit. The definition is in the upstream
[5.1.12 global configuration](https://github.com/fcitx/fcitx5/blob/5.1.12/src/lib/fcitx/globalconfig.cpp#L135).
Reword's default is now Ctrl+Alt+Shift+P in the controller, Store and addon.
Existing explicitly saved user bindings are preserved; users who explicitly
saved the old conflicting binding need to choose another in Settings. General
Fcitx/compositor conflict detection is still limited and needs a separate audit.

With the new default, the Qt native probe passes pause/resume, selected correction
and password boundaries, then a real 31-second expiry and Pause after that idle
interval. The final fingerprint/capability refactor is being revalidated in Qt
and GTK, followed by native X11 and independent review. No merge is claimed yet.
Temporary diagnostic key-event instrumentation used only in the synthetic
session was removed; no such logging is part of the change.

GTK reproduced another continuation failure after Qt passed: the client can
refresh surrounding text only before the next key. The key path now also checks
anticipated printable continuation (permission/bounds/unlock gated, excluding
shortcut modifiers). A real Backspace regression then failed because Fcitx maps
Backspace to control character U+0008, rather than zero. Predictive continuation
now excludes controls and Delete explicitly. Actual fourth/fifth attempts after
four Backspace presses must remain skipped/corrected respectively.

Independent review found no definite correctness issue in the reset/default
changes or the extra continuation path. A capability-only Password/Sensitive
transition during an unconfirmed native edit remains a specific coverage gap;
ordinary password exclusion does not prove that timing-sensitive case.

GTK client cursor changes revealed a separate Reset protocol path: the client
sends InputContextReset while its surrounding cursor/text snapshot still reflects
the old position. Temporary event-count diagnostics showed eight Resets for eight
Backspaces, two for Left/Right, and one for a client cursor change, without a new
surrounding snapshot. The addon now handles Reset, preserving only the one paired
with a deletion key in the same context. Focus loss and a new text snapshot invalidate that
exemption. It expires after 250 milliseconds and no-op deletion at a field edge
is excluded. A no-op Delete followed by client cursor movement first failed the
native GTK probe and is now part of its regressions.

Review also found a pending-confirmation ordering race. Tracking runs before the
final navigation reset, and the pending retry snapshot is reset too, so later
confirmation cannot reinstall the old cycle. Temporary Reset diagnostics have
been removed. Immediate clearing on key release was tried and reproduced a real Backspace
failure: GTK can deliver its asynchronous Reset after release. The bounded
post-key interval preserves that protocol ordering. Final native results are
still pending for these last two fixes.

The deletion/Reset pairing is a bounded correlation, not a client-provided edit
transaction ID. Clients that delay Reset beyond 250 milliseconds, or suppress
both Reset and text updates for a real deletion immediately followed by mouse
movement, remain an explicit compatibility limitation. The safest result of an
expired exemption is starting a fresh retry cycle. This does not expand the
claims to untested applications or physical session lock/unlock.


## Final local verification

All six CTest suites pass (15.46 seconds). Final private GNOME Qt and GTK native
runs both exit 0. They cover actual Backspace alternate attempts, no-op Delete
followed by client cursor changes, Left/Right, continuation, 31-second expiry,
explicit paused-state persistence and Pause after idle, Undo/code/password
boundaries. Qt also confirms selection/history; GTK still lacks a selection
anchor and refuses passage replacement unchanged. Three private native X11
cycles pass, including real delivery of the updated Pause default through XTest.
Independent final review has no remaining actionable finding in this scope.
Windows CI and integration remain pending; no final release acceptance is claimed.
