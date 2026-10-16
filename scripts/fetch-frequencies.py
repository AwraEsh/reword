#!/usr/bin/env python3
"""Build attributed offline ranking tables from pinned wordfreq data.

Developer-only requirements: GitHub CLI and msgpack. The app has no Python or
network dependency. Derived JSON keeps attribution and license inside each file.
"""
from pathlib import Path
import base64,gzip,hashlib,json,subprocess,sys,unicodedata
root=Path(__file__).resolve().parents[1]
try:import msgpack
except ImportError:
 sys.path.insert(0,str(root/'.deps/python-build'))
 import msgpack
commit='912caf64b657478d1dff1138efdc078947d54bb1'
def fetch(path):
 if path.endswith('.gz'):
  metadata=json.loads(subprocess.check_output(['gh','api',f'repos/rspeer/wordfreq/contents/{path}?ref={commit}']))
  blob=json.loads(subprocess.check_output(['gh','api',f"repos/rspeer/wordfreq/git/blobs/{metadata['sha']}"]))
  raw=base64.b64decode(blob['content']);assert hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()==metadata['sha']
  return raw
 return subprocess.check_output(['gh','api','--allow-escape-sequences','-H','Accept: application/vnd.github.raw+json',f'repos/rspeer/wordfreq/contents/{path}?ref={commit}'])
notice=fetch('NOTICE.md').decode();license_text=fetch('LICENSE.txt').decode()
target=root/'assets/dictionaries';target.mkdir(exist_ok=True)
(target/'wordfreq-NOTICE.md').write_text(notice)
(target/'wordfreq-code-license.txt').write_text(license_text)
manifest={}
for language,file,name,limit in [('en','large_en.msgpack.gz','en_US',100000),('fa','small_fa.msgpack.gz','fa_IR',50000)]:
 raw=fetch('wordfreq/data/'+file);data=msgpack.unpackb(gzip.decompress(raw),raw=False)
 assert data[0]=={'format':'cB','version':1}
 words={}
 for index,group in enumerate(data[1:]):
  zipf=900-index
  if zipf<200:break
  for word in group:
   if not word or len(word)>64 or not all(c.isalpha() or c in "'’\u200c-" for c in word):continue
   key=word.lower()
   if key not in words:words[key]=zipf
   if len(words)>=limit:break
  if len(words)>=limit:break
 source=f'https://github.com/rspeer/wordfreq/blob/{commit}/wordfreq/data/{file}'
 document={'format':'Reword frequency ranks, Zipf x100','license':'CC-BY-SA-4.0','licenseUrl':'https://creativecommons.org/licenses/by-sa/4.0/','source':source,'attribution':notice,'changes':'Filtered alphabetic tokens; lowercase keys; retained centibel-derived Zipf ranks. This is not the wordfreq tokenizer.','words':words}
 output=target/(name+'-frequency.json');output.write_text(json.dumps(document,ensure_ascii=False,separators=(',',':'))+'\n')
 manifest[output.name]={'source':source,'sourceSha256':hashlib.sha256(raw).hexdigest(),'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'words':len(words),'license':'CC-BY-SA-4.0'}
 print(language,len(words),'ranks generated',flush=True)
(target/'frequency-sources.json').write_text(json.dumps(manifest,indent=2)+'\n')
