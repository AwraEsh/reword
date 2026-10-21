# Fcitx operation deadline implementation plan

> Execute inline with superpowers:executing-plans. All C++/Qt runs use hosted CI.

**Goal:** refuse selected corrections more than ten seconds after acceptance,
including worker initialization and result-delivery delay.
**Spec:** docs/REQUIREMENTS.md, Selected text and rules.
**Architecture:** keep the existing async worker and guarded native completion.
Capture a monotonic deadline when accepting selection, carry the same deadline
through the worker and completion, and leave Undo's separate expiry unchanged.
**Files:** src/platform/fcitx_addon.cpp, tests/fcitx_addon_test.cpp, this plan and
docs/CURRENT_HANDOFF.md. No new runtime dependency or local heavy workload.

- [ ] Add a real shortcut-acceptance regression using the existing owned Fcitx,
  Store, private D-Bus and recovery fixture. Record native commit calls. After
  the actual acceptance path snapshots the field and acquires its operation lock,
  substitute a controlled unresolved future at the worker-result boundary. A
  successful immediate result must commit once. A successful result delivered
  after 10.1 seconds must not commit, alter original context or create history.
  Observe actual hosted RED before changing implementation.
- [ ] Start one monotonic ten-second deadline at acceptance. Check cancellation
  and expiry before worker initialization; pass remaining time to Engine. Reject
  expired results before field reads and again immediately before native commit.
  Keep worker lifetime/lock safe and reject partial results.
- [ ] Verify both regression cases, the full hosted Linux/Windows suites and an
  independent static review. Record source/run identities and limitations.

Review focus: deadline copied rather than restarted, setup time included,
post-policy/field-read expiry, valid in-time edits preserved, no history on late
results. Cooperative cancellation is not preemption of a stuck provider. UI
Cancel coordination and ordinary Wayland/application acceptance remain separate.
