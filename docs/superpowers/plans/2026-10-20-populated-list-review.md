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
