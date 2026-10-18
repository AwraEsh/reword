# Revalidate policy at native edit boundaries

The read-only operation-policy review reproduced writes after Pause or permission
revocation at b69b1a3. Live bursts retained options and permission across multiple
edits; selected completion checked cancellation before a potentially blocking
native read, and that read reused an earlier Access value. Undo had the same
final-read gap for cancellation and settings changes.

1. Add synthetic service regressions for Pause, application denial, disabled
   English, cancellation, and cancellation followed by prepare. Reproduce the
   live burst and final-selection-read failures before changing production code.
2. Refresh access after native reads. Compare configuration and a monotonic
   cancellation revision immediately before each native write. Do not let
   prepare erase a cancellation that invalidated an older operation.
3. Apply the boundary check to Undo and the equivalent Fcitx live path, retaining
   surrounding text and cursor identity checks. Keep adapter target validation.
4. Build all targets; run every CTest suite and the isolated Wayland Qt/GTK probes.
   Review the diff independently, then commit and submit through GitHub CI.

The source-upload branch remains separate. These checks do not establish broad
Windows desktop compatibility, actual lock/unlock acceptance or final installer
stability. No host desktop input is used by the regressions.

Evidence refinement: the first broad query-count experiment injected some
changes after an edit and was discarded as regression evidence. The final
selected test gates injection on the operation lock and verifies synthetic text
at injection; baseline selected denial is RED. Undo's timer queries remain a
caller-identification limit; do not call those cases a proven exact-call race.
