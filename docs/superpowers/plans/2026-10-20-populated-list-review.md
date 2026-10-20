# Populated desktop list review

Goal: verify the existing English Qt interface with realistic and bounded worst-case
local data before accepting its UI. Preserve the current layout, colors and copy
unless a demonstrated usability failure requires a targeted change.

No local GUI/build workloads: use hosted Linux/Windows UI tests and retained
synthetic captures. Empty-state screenshots do not establish populated-list
quality. No real application IDs, personal words, sentences or runtime data enter
fixtures or source.

- Seed detected applications with all permission states, a long unbroken
  executable basename and long path. Seed optional word-pair counts, English and
  Persian personal words, and replacements up to their existing accepted limits.
  Use real Store/Controller operations in temporary user-only test directories.
- Capture populated pages in both themes at minimum logical size. Scroll to the
  lower Languages content; preserve separate empty-state evidence.
- Inspect actual images. Add geometry/interaction regressions for demonstrated
  clipped controls, overlapping text, unreachable permission/remove/practice
  actions or lost keyboard focus. Observe failures before changing production QML.
- Apply only necessary width, wrap, elision or scrolling corrections. Keep full
  data available through existing actions and accessibility names. Never truncate
  stored text merely to fit a row.
- Run full hosted suites and inspect regenerated Linux/Windows images. Record
  source/run identities and bounds. Normal desktop/DPI/tray/screen-reader
  acceptance remains separate.

This plan records remaining UI work; no populated-list result is accepted yet.

Hosted run 39396895276 at 8288a35 reproduces hidden actions in Applications
(x=2993) and the personal-word button (width=908.6) at 880 pixels. Actual Linux
captures also show replacement text crossing Remove and overlapping Insights
word labels, despite their action-only bounds passing. Extend the regression
to check rendered text extents and pairwise overlap, preserving full label data.
Retain both themes before assertions. Observe this additional RED before fixing
all demonstrated layout defects together; no production QML changed yet.
Reports/captures are ignored under .deps/populated-red-8288a35-linux and the
failure log .deps/populated-red-8288a35.log.

Hosted RED 39397419557 at 0db2be6 reproduces all four populated surfaces:
application permission at x=2993; personal action width=908.6; replacement text
reaches x=2941.6 across Remove; Insights words overlap each other and Practice.
Both platform UI suites fail; Linux has 23 other UI cases passing. Saved log:
.deps/populated-text-red-0db2be6.log.

Let existing rows grow vertically for wrapped full text; release label minimum
width so controls keep their space. Bound personal-word buttons to their Flow,
wrap their label, and reserve enough permission-control width for Ask tomorrow.
No stored word/name/replacement is truncated. Reveal the complete replacement
row for captures rather than centering its now taller label around the button.
Full hosted tests and actual regenerated images remain required.

Full hosted push run 39397991807 and PR run 39397997914 pass at baf6773.
Inspected the actual Linux/Windows regenerated populated captures: full wrapped
text and actions fit. Linux diagnostic XML has no binding-loop, assignment,
ReferenceError or TypeError warnings. Independent code review found no actionable
sizing cycle, stored-value change or handler regression.

Follow-up real evidence: Windows dark ComboBox arrows are almost black. The
verified Qt 6.8.3 source archive's Basic/ComboBox.qml uses control.palette.dark
for indicator color; the current application does not set this palette role.
Add a real indicator/background contrast check (3:1 non-text minimum), and open
Practice from the populated Insights action with a 64-character accepted answer.
Retain both long-dialog captures and require its full bounds inside 880×620.
Observe hosted results before changing the palette role or dialog sizing.

Run 39398850249 at ef9d446 confirms indicator contrast 1.147:1 in dark mode on
both Qt versions. Bind the inherited palette.dark role to the existing muted
foreground; preserve control geometry and copy. Practice fails earlier than its
geometry assertion (opened=false), so do not change dialog sizing yet. Wait for
the actual controlled Service startup before fetching its delegate, and observe
active focus, click delivery, delegate lifetime and popup metadata. This separates
initial model-refresh timing from a callback/popup defect without bypassing the
real keyboard action or weakening the final bounds assertion.

Diagnostic run 39399695073 at a3fd67a passes indicator contrast and all prior
UI cases. The Practice action has correct active focus, a surviving delegate and
one clicked signal. Its test selected the wrong QObject: the search kept walking
after finding AppDialog and overwrote it with the internal PopupItem whose title
is forwarded, but which has no answer/opened properties (visible=1,width=880).
Require the actual answer/opened contract and stop at that object. This is a
fixture error, not an activation bug. Add question ink/Close bounds as well as
popup bounds, since Qt can cap popup size while unwrapped text still overflows.
Do not claim long-dialog geometry before this corrected test executes remotely.

Corrected test/source 3c4aaac in run 39400345037 reproduces the actual Practice
layout defect: full question ink is 1323.98 px in an 880 px viewport. All 28 other
UI cases pass. The actual dark Linux capture shows the question running off the
right edge while Qt caps the popup itself to the window. Log/captures retained
under .deps/practice-bounds-red-3c4aaac*. This is genuine geometry RED after
fixing the earlier object-selection error.

Bound Practice to 480 logical pixels or the window minus 48, make its existing
ColumnLayout the explicit content item, wrap the complete question and fill the
input width. Preserve the answer value and existing keyboard/Close behavior.
Full hosted verification and regenerated Linux/Windows images remain pending.
