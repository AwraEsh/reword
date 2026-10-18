# Keyboard and rendered UI evidence

The interface remains English. Content-language controls and mixed Persian/English
text are independent of the interface language. This document records evidence,
not a claim that every desktop or interaction has been accepted.

## Keyboard checks

The UI suite exercises modal focus, repeated Tab containment and Escape dismissal
for Pause, History detail, Clear history and Practice. It also visits all seven
sidebar pages using Tab/Space, then Backtab, and reaches/activates the explicit
No action in the permission dialog. That test verifies reachability and one button activation. A separate test
drives application discovery through Service, waits for the active modal to
close, selects No using Tab/Space, and verifies the stored denial and empty
history. Calendar rules have separate Store tests.

Initial focus checks already passed. These checks extend acceptance coverage;
they do not represent a repaired focus regression. An initial sidebar test used
QObject ownership to find Repeater delegates; it was corrected to inspect the
visual tree. That was a fixture error, not an application navigation failure.

A separate geometry test reproduced the long-executable-name header overflowing
the permission dialog. `Text.Wrap` keeps its full plain-text title within the
existing dialog width, including an unbroken 220-character basename. This was
observed RED before the one-line QML change.

## Rendered checks

Optional synthetic captures cover all seven pages plus Pause, populated bilingual
History detail, Clear history, Practice and the long consent title in light/dark
modes. The minimum logical viewport is 880x620. History fixtures contain repeated
mixed text and use the real scrolling/read-only text controls. No user text is
used for captures.

CI publishes separate Linux and Windows UI review artifacts from offscreen Qt
rendering with its software scene graph. Those are rendered OS/Qt evidence, not
proof of native desktop focus, tray, graphics-driver behavior or ordinary Windows
10/11 installation acceptance. Actual images must be downloaded and inspected;
a successful artifact upload does not itself establish a visual review.

Local Linux modal/No/navigation tests pass; all seven CTest suites pass in 38.63s.
Light History, dark Pause and dark Practice captures were inspected. The first
long-title capture was invalidated when a controller notification refreshed the
test's binding; the fixture now removes that binding using QQmlProperty::write.
Inspect the regenerated long-title image before treating it as visual proof.

## Remaining acceptance

Inspect the final Linux/Windows captures, including long title and mixed text;
complete populated Applications/Insights/personal dictionary/replacement list and
remaining dialog interactions; verify normal desktop scaling, shortcuts and tray.
These checks complement the full compatibility and active stability gates.

## CI findings still being repaired

The first capture runs 39151319903/39151365451 at 9a79a24 pass Linux tests but
show near-white heading/body text on light dialog surfaces under the older Qt
SDK. Windows UI tests fail; that GUI executable produced no useful stdout in the
initial CTest log. The branch now adds an actual contrast check and retained
JUnit/partial screenshot diagnostics. These are open issues, not accepted visual
results. Earlier local success used Qt 6.8.2 and cannot replace the CI findings.

At 9ee9e87 the new Linux regression confirms all four headings have contrast
1.0638 in light mode (Qt 6.4.2). Windows JUnit identifies four Tab-containment
failures; opening focus, sidebar navigation and the No action are separate tests.
A shared AppDialog now binds palette roles directly to the current theme. The
Windows focus failure includes its active item/window identity and a failure
capture so native-window routing can be distinguished from actual escaped focus.
Local Qt 6.8.2 passes all 19 UI cases; final CI is still required.

At 8c77a0b, direct popup palette bindings still failed the older Qt contrast
check (1.1417 in light mode). Windows diagnostics showed focus on the No button
of a newly discovered application's consent prompt, over the active modal.
The consent presenter now waits for other dialogs to become invisible, including
their closing transitions. A deterministic Service-to-QML test reproduced the
original overlap before this change. It passes with the deferred presenter and
verifies a keyboard No decision is persisted. Pure UI fixtures now inject a
metadata-only adapter; they do not discover CI/host foreground applications.
Native adapter integration tests remain separate.

The application now sets its initial and changed theme palette for both Widgets
and Quick controls. The previous window-only palette left shared popup defaults
and tray menu colors out of sync. A widget palette regression failed before this
change and passes locally afterwards. Linux Qt 6.4 and Windows CI are still
required before accepting the popup contrast repair.

After these changes, the full local build and all seven CTest suites pass in
61.28s with Qt 6.8.2. The regenerated light long-name consent capture was
inspected: the complete title wraps and all three actions remain visible.
Independent read-only code review found no important issues in this diff. This
local evidence does not close the older Qt/Windows or physical desktop gates.

At 63e1fc2, Windows CI passes including the controlled queue/keyboard No check,
but manual review finds missing-glyph boxes even for English in its offscreen
captures. This invalidates their typography evidence. A shaping/glyph regression
now requires real English/Persian glyphs instead of accepting image creation alone.
Qt's Windows offscreen plugin uses the FreeType font database rather than the
native Windows font database; the capture environment needs its installed fonts.

The same source still fails all four heading contrasts under Linux Qt 6.4.2.
Application-level palette synchronization fixes Widgets but does not repair
popup child palette inheritance there. The shared dialog now owns its plain-text
wrapped heading, and popup labels/editors bind directly to the theme ink. The
practice editor also owns its background. Contrast checks cover visible text in
popup contents and footers against their nearest opaque control background.
Fresh CI and downloaded rendered review are required again.

Local verification of the direct ink/header changes and glyph/expanded contrast
checks: full build and all seven CTest suites pass in 66.17s. Three controlled
native X11 cycles at 63e1fc2 also pass first/second/third correction, live/selected
Undo, Pause, selected history and password exclusion. Their initial local launch
attempts lacked the rootless window-manager theme path; setting the private
XDG_DATA_DIRS fixed the fixture environment without changing production code.

At 3ea55cb, the four Linux headings and other popup text pass; the expanded
check reproduces low-contrast Practice placeholder text (2.80893, light mode).
Its placeholder now binds to the theme's muted ink. The first glyph-index check
passed on Windows despite its box-font screenshots; glyph indices alone are
insufficient. It now also requires discovered font families and valid raw fonts.
Do not call that first check a reproduced regression or accepted typography.

At dbd25e3, Linux CI is green and Windows now reproduces the missing-font defect:
its font-family inventory is empty, and JUnit reports the nonexistent SDK
lib/fonts path. The Windows workflow now points QT_QPA_FONTDIR at the runner's
installed Windows Fonts directory after checking Segoe UI exists. This changes
the offscreen test environment; no fonts are copied into the app or artifacts.
Fresh Windows CI and actual images must confirm this repair.
