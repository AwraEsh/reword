#!/usr/bin/env python3
"""Observe a real offscreen app and its helper without recording user content."""
import argparse, json, os, pathlib, subprocess, time, datetime, signal
parser=argparse.ArgumentParser();parser.add_argument('--minutes',type=float,default=120);parser.add_argument('--output',default='.deps/soak');args=parser.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];output=(root/args.output).resolve();output.mkdir(parents=True,exist_ok=True)
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',QT_QPA_PLATFORMTHEME='',REWORD_DATA_DIR=str(output/'data'))
process=subprocess.Popen([str(root/'build/reword')],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
report={'pid':process.pid,'startedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'targetSeconds':args.minutes*60,'state':'running','samples':0,'maxRssKiB':0,'appCpuSeconds':0,'helperMaxRssKiB':0}
started=time.monotonic();ticks=os.sysconf('SC_CLK_TCK');baseline=None
try:
 while time.monotonic()-started<args.minutes*60:
  if process.poll() is not None:report['state']='failed';report['exitCode']=process.returncode;break
  status=pathlib.Path(f'/proc/{process.pid}/status').read_text();rss=int(next(line.split()[1] for line in status.splitlines() if line.startswith('VmRSS:')))
  fields=pathlib.Path(f'/proc/{process.pid}/stat').read_text().split(') ',1)[1].split();cpu=(int(fields[11])+int(fields[12]))/ticks
  if baseline is None and time.monotonic()-started>10:baseline=rss
  children=set()
  for task in pathlib.Path(f'/proc/{process.pid}/task').iterdir():
   try:children.update((task/'children').read_text().split())
   except FileNotFoundError:pass
  helper_rss=0
  for child in children:
   try:
    s=pathlib.Path(f'/proc/{child}/status').read_text();helper_rss+=int(next(line.split()[1] for line in s.splitlines() if line.startswith('VmRSS:')))
   except (FileNotFoundError,StopIteration):pass
  report.update(samples=report['samples']+1,elapsedSeconds=round(time.monotonic()-started,2),lastRssKiB=rss,maxRssKiB=max(rss,report['maxRssKiB']),helperMaxRssKiB=max(helper_rss,report['helperMaxRssKiB']),appCpuSeconds=cpu)
  if baseline is not None:report['rssGrowthKiB']=rss-baseline
  (output/'report.tmp').write_text(json.dumps(report,indent=2)+'\n');(output/'report.tmp').replace(output/'report.json');time.sleep(5)
 else:report['state']='passed'
finally:
 process.terminate()
 try:process.wait(timeout=15)
 except subprocess.TimeoutExpired:process.kill();process.wait();report['shutdownForced']=True
 report['elapsedSeconds']=round(time.monotonic()-started,2);report['finishedUtc']=datetime.datetime.now(datetime.timezone.utc).isoformat();(output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2),flush=True)
raise SystemExit(0 if report['state']=='passed' else 1)
