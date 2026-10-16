"""Two launches must share one background app, with no external text involved."""
import os, pathlib, subprocess, sys, tempfile, time
with tempfile.TemporaryDirectory(prefix='reword-activation-') as directory:
 env=dict(os.environ,REWORD_DATA_DIR=directory,QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',QT_QPA_PLATFORMTHEME='')
 first=subprocess.Popen([sys.argv[1],'--smoke-test'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 try:
  deadline=time.monotonic()+5
  while not (pathlib.Path(directory)/'instance.lock').exists():
   if first.poll() is not None:raise AssertionError('First launch failed')
   if time.monotonic()>deadline:raise AssertionError('Instance lock was not created')
   time.sleep(.02)
  started=time.monotonic();second=subprocess.run([sys.argv[1],'--smoke-test'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=5)
  assert second.returncode==0, 'Second launch did not activate existing app'
  assert time.monotonic()-started<2.9, 'Second launch started another full application'
  assert first.poll() is None, 'Existing application stopped unexpectedly'
  assert first.wait(timeout=8)==0, 'Existing app did not shut down cleanly'
 finally:
  if first.poll() is None:first.terminate();first.wait(timeout=5)
print('PASS: second launch activates the existing instance.')
