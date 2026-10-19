"""Bounded shutdown of processes and desktop handles owned by one workload."""
import os
import subprocess


def stop_child(process, grace):
    """Reap our Popen child, escalating only if it is still alive."""
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=grace)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=grace)


def cleanup_owned(fixture, app, watches, desktop, grace=3):
    result = {'forced': False, 'failed': False}

    def attempt(action):
        try:
            action()
        except Exception:
            result['failed'] = True

    def stop_fixture():
        if fixture.poll() is None:
            try:
                os.set_blocking(fixture.stdin.fileno(), False)
                os.write(fixture.stdin.fileno(), b'quit\n')
                fixture.wait(timeout=grace)
            except (OSError, subprocess.TimeoutExpired):
                result['forced'] = True
                stop_child(fixture, grace)

    def stop_app():
        if app.poll() is None:
            try:
                desktop.close_app(app.pid)
                app.wait(timeout=grace)
            except Exception:
                result['forced'] = True
                stop_child(app, grace)

    if fixture is not None:
        attempt(stop_fixture)
        for stream in (fixture.stdin, fixture.stdout):
            if stream is not None:
                attempt(stream.close)
    if app is not None:
        attempt(stop_app)
    if 'helper' in watches:
        def stop_helper():
            if watches['helper'].stop(grace):
                result['forced'] = True
        attempt(stop_helper)
    for watch in watches.values():
        attempt(watch.close)
    attempt(desktop.close)
    return result
