# Native selection operation UI

Execute inline; hosted C++/Qt verification only after two host freezes.

Problem: Service accepts a native selection without telling Controller that an
operation started. The real Cancel button is bound to Controller.busy, so users
cannot cancel this operation from the GUI. Some completion paths emit only a
message, so tracking only result is insufficient. Fcitx IPC is separate scope.

- [x] Add real Controller/QML/Service integration rows for successful correction,
  Cancel and unchanged text. Hold only the test process worker pool to observe
  the queued interval; release on every exit before Controller teardown. Trigger
  the actual adapter action, require a visible Cancel, invoke its clicked signal,
  and check resulting field/history plus cleared busy state. Observe hosted RED.
- [ ] Publish accepted-operation lifecycle from Service on every terminal path.
  Keep queued workspace state distinct from active service state so a rejected
  competing request cannot hide Cancel. Preserve cooperative cancellation and
  the existing single-operation lock. Add targeted overlap coverage if needed.
- [ ] Full hosted Linux/Windows suites, independent review, source/run identities
  and handoff. Ordinary native desktop and Fcitx Cancel remain separate.

Mutation target: suppress native operation-start notification and the real
Cancel stays hidden despite an accepted operation. Missing terminal delivery
leaves busy stuck on unchanged/early-return paths; ignoring Cancel changes the
field/history in the cancellation row. No production test-only methods.

Hosted RED 39543497761 at e905973: downloaded Linux UI JUnit has exactly
three failures, one per new row, each at controller.busy() remaining false after
the accepted operation lock exists. No fixture/compile errors. Implementation
now emits Service activity after accepting its future and from a scope guard
after every completion path releases the lock. Controller combines active
Service state with queued workspace state. Competing workspace requests cannot
hide Cancel or reset accepted work. GREEN and independent review are pending.

First GREEN attempt 39543975378 at bb76c1b passes the busy assertion on both
systems but fails all three Cancel visibility checks. The original lookup used
the first object whose text was Cancel, which is ambiguous with hidden dialog
standard buttons. Give the workspace action an explicit object name and use it
in the integration test; do not weaken the actual visibility assertion. Fresh
hosted verification will distinguish the selector issue from a UI defect.
