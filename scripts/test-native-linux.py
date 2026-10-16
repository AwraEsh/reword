#!/usr/bin/env python3
"""Run native GTK probes on a private X server and private accessibility bus.

The lock service is a controlled fixture, not proof of a physical lock screen.
No host application or display is read or modified.
"""
import os,pathlib,subprocess,sys,tempfile,time,shutil,secrets
root=pathlib.Path(__file__).resolve().parents[1]
if '--inside' not in sys.argv:
 result=subprocess.run(['dbus-run-session','--',sys.executable,__file__,'--inside',*sys.argv[1:]])
 raise SystemExit(result.returncode)
server=shutil.which('Xvfb') or str(root/'.deps/xvfb/usr/bin/Xvfb')
if not pathlib.Path(server).exists():raise SystemExit('Install Xvfb to run the controlled native probes.')
with tempfile.TemporaryDirectory(prefix='reword-native-') as directory:
 folder=pathlib.Path(directory);(folder/'runtime').mkdir(mode=0o700)
 number=4000+secrets.randbelow(16000)
 while pathlib.Path(f'/tmp/.X{number}-lock').exists():number+=1
 display=f':{number}';authority=folder/'Xauthority'
 subprocess.run(['xauth','-f',str(authority),'add',display,'.',secrets.token_hex(16)],check=True,stderr=subprocess.DEVNULL)
 env=dict(os.environ,DISPLAY=display,XAUTHORITY=str(authority),XDG_RUNTIME_DIR=str(folder/'runtime'),XDG_SESSION_TYPE='x11',XDG_CONFIG_HOME=str(folder/'config'),XDG_CACHE_HOME=str(folder/'cache'),XDG_DATA_HOME=str(folder/'data'),GTK_IM_MODULE='gtk-im-context-simple',GSETTINGS_BACKEND='memory')
 env.pop('AT_SPI_BUS_ADDRESS',None);env.pop('WAYLAND_DISPLAY',None);env['GDK_BACKEND']='x11'
 # Activated accessibility services must inherit this private display/runtime too.
 from gi.repository import Gio,GLib
 activation={key:env[key] for key in ['DISPLAY','XAUTHORITY','XDG_RUNTIME_DIR','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_DATA_HOME','XDG_SESSION_TYPE','GDK_BACKEND','GSETTINGS_BACKEND']}
 bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
 bus.call_sync('org.freedesktop.DBus','/org/freedesktop/DBus','org.freedesktop.DBus','UpdateActivationEnvironment',GLib.Variant('(a{ss})',(activation,)),None,Gio.DBusCallFlags.NONE,2000,None)
 children=[]
 try:
  log=(folder/'xvfb.log').open('wb');xserver=subprocess.Popen([server,display,'-screen','0','1280x800x24','-nolisten','tcp','-auth',str(authority)],env=env,stdout=log,stderr=log);children.append(xserver)
  deadline=time.monotonic()+5
  while not pathlib.Path(f'/tmp/.X11-unix/X{number}').exists():
   if xserver.poll() is not None:raise RuntimeError('Private X server failed to start.')
   if time.monotonic()>deadline:raise RuntimeError('Private X server did not become ready.')
   time.sleep(.05)
  bus.call_sync('org.a11y.Bus','/org/a11y/bus','org.freedesktop.DBus.Properties','Set',GLib.Variant('(ssv)',('org.a11y.Status','IsEnabled',GLib.Variant('b',True))),None,Gio.DBusCallFlags.NONE,3000,None)
  lock=subprocess.Popen([sys.executable,str(root/'tests/test_lock_service.py')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(lock)
  for _ in range(40):
   ready=subprocess.run(['gdbus','call','--session','--dest','org.gnome.ScreenSaver','--object-path','/org/gnome/ScreenSaver','--method','org.gnome.ScreenSaver.GetActive'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
   if ready.returncode==0:break
   time.sleep(.05)
  else:raise RuntimeError('Controlled lock service failed to start.')
  repeat=int(os.environ.get('REWORD_NATIVE_REPEAT','3'))
  for index in range(repeat):
   for probe in ['reword_native_probe','reword_native_service_probe']:
    result=subprocess.run([str(root/'build'/probe)],env=env,timeout=40)
    if result.returncode:raise RuntimeError(f'{probe} failed on run {index+1}.')
  print(f'PASS: {repeat} controlled native probe cycles.',flush=True)
 finally:
  for child in reversed(children):
   child.terminate()
   try:child.wait(timeout=5)
   except subprocess.TimeoutExpired:child.kill();child.wait()
