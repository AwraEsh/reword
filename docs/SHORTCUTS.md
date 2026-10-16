# Shortcut registration

When a new native shortcut set cannot be registered, preserve the previous
working configuration and attempt to restore its registrations. Report whether
restoration succeeded. Revert stored settings only if they still match the
failed attempt; a newer user edit must survive and be checked on the next tick.
Increment the settings revision when reverting so pending text work cannot apply
under an obsolete configuration. Initial unavailable native registration must
remain visible; Wayland Fcitx shortcuts are handled inside supported fields and
must not be misrepresented as compositor-wide registration.

Verification starts with Service regressions for restoration, newer settings
during failed registration, and unavailable rollback. Actual native conflict
probes and the full suite follow. No user text or clipboard data is involved.

## Implementation and evidence

Rollback compares both the complete shortcut tuple and configRevision inside a
SQLite transaction. A mismatch causes a fresh registration attempt, including
an X-to-Y-to-X edit. A storage exception leaves the attempt retryable; native
health is reported even when persistence fails. If the previous registrations
cannot be recovered, the settings are restored and the UI reports global
shortcuts unavailable. Re-saving that configuration retries registration when
its revision changes. Wayland settings remain available to Fcitx even though
Reword has no compositor-wide registration API there.

Service regressions first reproduced failed restoration, recovery after a real
SQLite writer lock, newer-revision handling and re-saving unavailable bindings.
The corrected implementation passes these checks along with the existing typing,
Undo and password boundaries. A synthetic Wayland case confirms unavailable
native-global bindings do not roll back Fcitx configuration or report a conflict.

Three controlled native X11 cycles pass on the current Debian host. Another
native connection reserves the new shortcut; Service reverts settings and the
previous Pause binding is physically delivered through XTest on the private
X server. The Windows native probe reserves a Win32 hotkey and uses SendInput to
verify the restored Pause binding; Windows compilation and execution require
fresh CI and are not yet counted as passed.

Native X11 tests need Xvfb, Openbox, Xauth, xprop, AT-SPI and libXtst (development
headers for the probe build). Local missing tools were extracted under ignored
.deps/native-x11 without installing system packages. Openbox's extracted theme
resources require that prefix in XDG_DATA_DIRS. This is a developer/test setup,
not an end-user application dependency.

The first local X11 run passed all assertions but failed deleting a private GVFS
mount. The caller now owns the temporary directory outside the private D-Bus
session, disables GVFS/FUSE activation and detaches only its own mountpoints after
that session ends. Fresh three-cycle runs complete normally. No host display,
clipboard, desktop settings or user application text are used.
