#!/usr/bin/env python3
"""Fetch pinned, redistributable dictionaries for builds; never used at runtime."""
from pathlib import Path
from urllib.request import urlopen
import hashlib, json
commit='8cfea406b505e4d7df52d5a19bce525df98c54ab'
root=Path(__file__).resolve().parents[1]/'assets/dictionaries';root.mkdir(parents=True,exist_ok=True)
manifest={}
for language,name in [('en','en_US'),('fa','fa_IR')]:
 for original,target in [('index.aff',name+'.aff'),('index.dic',name+'.dic'),('license',name+'-license.txt')]:
  url=f'https://raw.githubusercontent.com/wooorm/dictionaries/{commit}/dictionaries/{language}/{original}'
  data=urlopen(url,timeout=60).read()
  (root/target).write_bytes(data)
  manifest[target]={'source':url,'sha256':hashlib.sha256(data).hexdigest()}
(root/'sources.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Pinned dictionaries downloaded with their original licenses.')
