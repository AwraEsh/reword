#!/usr/bin/env python3
"""Owned packaged-GUI workload runner; see the stability plan for acceptance scope."""
from stability_evidence import (Evidence, FixtureProtocol, ProcessWatch, WorkloadFailure,
                                wait_until, write_report)

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import time

from active_desktop import OwnedDesktop
from active_fixture import FixtureClient
from active_cleanup import cleanup_owned
from active_history import HistoryCheck
from process_metrics import read_child_pids


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def seed_database(directory, fixture, executable):
    directory.mkdir(mode=0o700)
    database = directory / 'reword.sqlite'
    with sqlite3.connect(database) as connection:
        connection.execute('CREATE TABLE settings(key TEXT PRIMARY KEY,value TEXT NOT NULL)')
        connection.execute('CREATE TABLE applications(id TEXT PRIMARY KEY,name TEXT NOT NULL,'
                           'decision TEXT NOT NULL DEFAULT \'unknown\',remind TEXT)')
        connection.execute('INSERT INTO applications VALUES(?,?,?,NULL)',
                           (str(fixture), 'Synthetic fixture', 'allow'))
        connection.execute('INSERT INTO applications VALUES(?,?,?,NULL)',
                           (str(executable), 'Workload GUI', 'deny'))
        for key, value in {'history': True, 'historyCount': 100, 'historyMiB': 20,
                           'historyDays': 30, 'statistics': False}.items():
            connection.execute('INSERT INTO settings VALUES(?,?)', (key, json.dumps([value])))
    database.chmod(0o600)
    return database


def run_owned(args):
    os.umask(0o077)
    desktop = OwnedDesktop(args.owned_directory)
    root = Path(args.owned_directory)
    app_run, fixture_path = Path(args.app).resolve(strict=True), Path(args.fixture).resolve(strict=True)
    executable, helper = app_run.parent / 'usr/bin/reword', app_run.parent / 'usr/bin/reword-native-helper'
    if app_run.name != 'AppRun' or any(not p.is_file() or p.is_symlink() for p in [app_run, executable, helper, fixture_path]):
        desktop.close()
        raise WorkloadFailure('test-infrastructure')
    output = Path(args.output)
    output.mkdir(mode=0o700, parents=True, exist_ok=False)
    database = seed_database(root / 'gui-data', fixture_path, executable)
    environment = dict(os.environ, REWORD_DATA_DIR=str(database.parent), QT_QPA_PLATFORM='xcb',
                       QT_QUICK_BACKEND='software', QT_QPA_PLATFORMTHEME='')
    evidence = Evidence()
    metadata = {'scope': 'owned-X11-development-workload',
                'productReleaseAccepted': False,
                'startedUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'targetSeconds': args.seconds, 'binarySha256': sha256(executable),
                'helperSha256': sha256(helper), 'launcherSha256': sha256(app_run),
                'resources': {}, 'shutdownForced': False, 'stage': 'startup'}
    watches, app, fixture, client = {}, None, None, None
    cancelled = False
    started, last_sample, baseline = time.monotonic(), -1.0, False
    previous_handlers = {}

    def cancel(*_):
        nonlocal cancelled
        cancelled = True

    for number in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[number] = signal.signal(number, cancel)

    def publish():
        if client:
            metadata['lastSnapshotCharacters'] = client.last_snapshot_characters
        write_report(output / 'report.json', {**metadata, **evidence.report(),
                     'elapsedSeconds': round(time.monotonic() - started, 3)})

    def tick(force=False):
        nonlocal last_sample
        if client:
            client.pump()
        if app and app.poll() is not None:
            raise WorkloadFailure('app-process-exit')
        now = time.monotonic()
        if force or now - last_sample >= 1:
            desktop.check_alive()
            for role, watch in watches.items():
                sample = watch.sample(now - started)
                metadata['resources'][role] = sample
                evidence.counter(role + '-sampled')
                rss_limit = (512 if role == 'app' else 256) * 1024
                if (sample['rssKiB'] > rss_limit or sample['swapKiB'] > 0 or
                        (baseline and (sample['privateGrowthKiB'] > 64 * 1024 or
                         (sample['elapsedSeconds'] >= 30 and sample['cpuPercent'] > 10)))):
                    raise WorkloadFailure('resource-budget')
            last_sample = now
            publish()
        if cancelled:
            raise WorkloadFailure('cancelled')

    def wait(predicate, timeout=10):
        return wait_until(predicate, timeout=timeout, tick=tick, cancelled=lambda: cancelled)

    def settle(seconds=.2):
        until = time.monotonic() + seconds
        while time.monotonic() < until:
            tick()
            time.sleep(.01)

    def setting(key):
        with sqlite3.connect(f'file:{database}?mode=ro', uri=True, timeout=.5) as connection:
            row = connection.execute('SELECT value FROM settings WHERE key=?', (key,)).fetchone()
            return json.loads(row[0])[0] if row else None

    history_check = HistoryCheck(database, evidence)
    history = history_check.count

    original, corrected = 'im here with teh book', 'I am here with the book'

    def selected(undo=True, source=original, result=corrected):
        metadata['stage'] = 'selected-source'
        before = history_check.latest_id()
        client.command('text:' + source)
        client.command('select-all')
        evidence.expect_text(source, client.snapshot())
        # GTK acknowledges commands before all AT-SPI text/caret events have
        # reached the helper. Let that synthetic setup settle before invoking
        # the user's shortcut; live word latency has no such added delay.
        settle(.12)
        desktop.shortcut('fix')
        metadata['stage'] = 'selected-result'
        client.wait_text(result)
        metadata['stage'] = 'selected-history'
        wait(lambda: history_check.inserted_after(before, source, result))
        evidence.counter('selected-fix')
        evidence.counter('history-insert')
        history()
        if undo:
            metadata['stage'] = 'selected-undo'
            desktop.shortcut('undo')
            client.wait_text(source)
            evidence.counter('selected-undo')

    def live():
        metadata['stage'] = 'live'
        client.command('text:reset ')
        settle()
        client.command('text:')
        settle()
        for attempt in (1, 2, 3):
            before = client.protocol.changes[-1][0] if client.protocol.changes else 0
            desktop.type_mistake()
            if attempt == 2:
                settle(.3)
                evidence.expect_text('teh ', client.snapshot())
                evidence.counter('live-second')
            else:
                client.wait_text('the ')
                changes = [value for value in client.protocol.changes if value[0] > before]
                # Four real GTK key edits end at the word boundary; subsequent
                # changes belong to the verified native correction.
                if len(changes) < 5 or changes[0][0] != before + 1:
                    raise WorkloadFailure('fixture-protocol')
                evidence.latency(changes[-1][1] - changes[3][1])
                evidence.counter('live-first' if attempt == 1 else 'live-third')
            if attempt != 3:
                client.command('text:')
                settle()
        desktop.shortcut('undo')
        client.wait_text('teh ')
        evidence.counter('live-undo')

    try:
        publish()
        app = subprocess.Popen([str(app_run)], env=environment, cwd=app_run.parent,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        watches['app'] = ProcessWatch(app.pid, 'app')
        def find_helper():
            for pid in read_child_pids(app.pid):
                try:
                    if Path(f'/proc/{pid}/exe').resolve(strict=True) == helper.resolve():
                        return pid
                except FileNotFoundError:
                    continue
            return None
        helper_pid = wait(find_helper, timeout=15)
        watches['helper'] = ProcessWatch(helper_pid, 'helper')
        wait(lambda: desktop.normal_window(app.pid), timeout=15)
        fixture = subprocess.Popen([str(fixture_path)], env=dict(environment,
                                   REWORD_FIXTURE_LIFETIME_SECONDS=str(min(18000, args.seconds + 1800)),
                                   REWORD_FIXTURE_CHANGE_TIMING='1'),
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.DEVNULL)
        client = FixtureClient(fixture, tick=tick, cancelled=lambda: cancelled)
        evidence.expect_text('teh ', client.snapshot(timeout=10))
        settle(1)
        # A real registered shortcut and database acknowledgment prove startup.
        desktop.shortcut('pause')
        wait(lambda: setting('pauseUntil') == -1)
        desktop.shortcut('pause')
        wait(lambda: setting('pauseUntil') == 0)
        live()
        selected()
        # Keep the dense history-cap exercise separate from sustained typing.
        for _ in range(109):
            selected(undo=False)
        if history() != 100:
            raise WorkloadFailure('history-bound')
        evidence.counter('history-pruned')
        metadata['historyBurstResources'] = dict(metadata['resources'])
        from process_metrics import ProcessSeries
        for watch in watches.values():
            watch.series = ProcessSeries()
        baseline = True
        metadata['resourceBaselineSeconds'] = round(time.monotonic() - started, 3)
        last_sample = -1
        active_started = time.monotonic()
        while time.monotonic() - active_started < args.seconds:
            live()
            selected()
            metadata['stage'] = 'pause'
            desktop.shortcut('pause')
            wait(lambda: setting('pauseUntil') == -1)
            client.command('text:')
            evidence.expect_text('', client.snapshot())
            desktop.type_mistake()
            settle(.3)
            evidence.expect_text('teh ', client.snapshot())
            evidence.counter('pause')
            desktop.shortcut('pause')
            wait(lambda: setting('pauseUntil') == 0)
            metadata['stage'] = 'protected'
            client.command('password')
            client.command('text:')
            settle()
            count = history()
            desktop.type_mistake()
            desktop.shortcut('fix')
            settle(.4)
            evidence.expect_text('teh ', client.snapshot())
            if history() != count:
                raise WorkloadFailure('history-bound')
            evidence.counter('protected')
            client.command('text:')
            client.command('plain')
            settle()
            evidence.counter('cycles')
        # Leave room for the two extra corrected characters below the limit.
        # Without a limit guard, the over-limit counterpart would also change.
        suffix = ' ' + 'a' * (49998 - len(original) - 1)
        selected(undo=False, source=original + suffix, result=corrected + suffix)
        evidence.counter('selection-limit-control')
        oversized = original + suffix + 'aaa'
        metadata['stage'] = 'oversized-selection'
        count = history()
        client.command('text:' + oversized)
        client.command('select-all')
        evidence.expect_text(oversized, client.snapshot())
        desktop.shortcut('fix')
        settle(1)
        evidence.expect_text(oversized, client.snapshot())
        if history() != count:
            raise WorkloadFailure('history-bound')
        evidence.counter('oversized-selection')
        evidence.counter('duration-complete')
        wait_until(lambda: True, tick=lambda: tick(force=True), cancelled=lambda: cancelled)
        evidence.finish()
    except WorkloadFailure as error:
        evidence.fail(error.reason)
    except Exception:
        evidence.fail('test-infrastructure')
    finally:
        try:
            result = cleanup_owned(fixture, app, watches, desktop)
            metadata['shutdownForced'] = result['forced']
            metadata['cleanupFailed'] = result['failed']
            if (result['forced'] or result['failed']) and evidence.state == 'passed':
                evidence.fail('shutdown-forced')
            if cancelled:
                evidence.fail('cancelled')
            metadata['finishedUtc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
            publish()
        finally:
            for number, handler in previous_handlers.items():
                signal.signal(number, handler)
    print(json.dumps({'state': evidence.state, 'failure': evidence.failure,
                      'latency': evidence.report()['latency']}, sort_keys=True), flush=True)
    return 0 if evidence.state == 'passed' else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--owned-directory', required=True)
    parser.add_argument('--app', required=True)
    parser.add_argument('--fixture', required=True)
    parser.add_argument('--seconds', type=int, required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if not 5 <= args.seconds <= 14400:
        parser.error('The active duration must be between 5 and 14400 seconds.')
    return run_owned(args)


if __name__ == '__main__':
    raise SystemExit(main())
