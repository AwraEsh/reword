# Per-application correction modes

Approved requirements: REQUIREMENTS.md, per-application mode section.
Keep notification work independently reviewable before starting these changes.

## Store and compatibility

- Add `ApplicationMode { Automatic, ShortcutOnly, Disabled }` and
  `Store::applicationMode(const QString &) const` in core headers/store.
- Represent modes using existing decisions: `allow` = Automatic,
  `shortcut` = ShortcutOnly, `deny` = Disabled. Existing unknown/later remain
  unauthorized. No text-data migration or additional mode column is needed.
- Old clients recognize only allow, so shortcut must fail closed on an older
  resident Fcitx addon rather than allowing unwanted live corrections.
- Explicit mode changes update decision and configRevision atomically. Preserve
  Not now's local-calendar reminder and No's persistent suppression.
- Tests: legacy allow/deny/later/unknown mapping, shortcut restart persistence,
  immediate visibility across connections, revision updates and rollback on
  failed storage changes. Do not cache policy across calls.

## Native service and Fcitx

- Update authorization for explicit selection/Undo to recognize shortcut.
- Gate live correction before text reads/ranking for ShortcutOnly. Metadata
  discovery stays available. Preserve selection Undo during metadata-only
  observation, but clear it for unsafe fields, identity changes or Disabled.
- Enforce the same split in Fcitx live events versus explicit selection actions;
  do not weaken password/surrounding-text capability guards.
- Switching mode cancels outstanding native work through revision checks, with
  final revalidation before writes and recovery/history only at existing safe
  boundaries. Never mark a field supported because its app is authorized.
- Tests: Auto first/second/third cycle; ShortcutOnly typing performs zero text
  reads/edits/counts while explicit selected fix and guarded Undo work; Disabled
  performs no reads/edits/history. Cover changes during queued/delivering jobs,
  password entry and focus changes. Test both Service and Fcitx ownership paths.

## Controller and UI

- Add `Controller::setApplicationMode(QString id, QString mode)` accepting
  automatic/shortcut/disabled. Use one validated decision path shared with
  existing consent/settings handling; invalidate pending notification tokens
  when any explicit matching app decision is made. Do not let a late Yes
  override ShortcutOnly or Disabled.
- Applications dropdown labels: Automatic, Shortcut only, Disabled. Show
  pending/Not now authorization separately. Consent Yes must clearly describe
  the default automatic mode for a newly authorized app.
- Tests: keyboard dropdown selection/persistence, immediate policy change,
  current notification invalidation and all three actual behaviors. Keep UI
  English and avoid implying unsupported fields can be edited.

## Acceptance

Run resource-bounded local suites/native probes, then Linux and Windows CI.
Real application typing/selection on both platforms remains required; build
success or a metadata consent popup alone does not prove correction support.
Update compatibility evidence and installer upgrade checks before stable release.
