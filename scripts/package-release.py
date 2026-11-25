"""Build the NEXUS Windows installer and explicit binary/source packages."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,zipfile
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser();p.add_argument('--staging',required=True,type=Path);p.add_argument('--out',type=Path,default=root/'dist/1.0');args=p.parse_args()
stage=args.staging.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
payload=root/'installer/payload.zip'
files={}
for name in ['obs-vst3-nexus.dll','nexus_vst3_worker.exe']:files['bin/64bit/'+name]=stage/'obs-plugins/64bit'/name
for file in (stage/'data/obs-plugins/obs-vst3-nexus').rglob('*'):
 if file.is_file():files['data/'+file.relative_to(stage/'data/obs-plugins/obs-vst3-nexus').as_posix()]=file
for name in ['README.md','LICENSE','LICENSES.md','THIRD-PARTY-NOTICES.md']:files['data/'+name]=root/name
for file in (root/'docs').rglob('*'):
 if file.is_file() and 'reports' not in file.parts: files['data/docs/'+file.relative_to(root/'docs').as_posix()]=file
for path in files.values():assert path.is_file(),path
# Documentation excludes test-product/vendor identities; required dependency notices remain.
blocked=os.environ.get('NEXUS_PRIVATE_DOC_BLOCKLIST','')
for name,file in files.items():
 if blocked and file.suffix.lower() in ['.md','.txt']:
  assert not re.search(blocked,file.read_text(encoding='utf-8-sig',errors='replace'),re.I),name
with zipfile.ZipFile(payload,'w',zipfile.ZIP_DEFLATED) as z:
 for name,file in sorted(files.items()):z.write(file,name)
exe=out/'OBS-VST3-NEXUS-1.0-Windows-x64-Setup.exe'
csc=Path(os.environ['WINDIR'])/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
command=[str(csc),'/nologo','/target:winexe','/platform:x64','/optimize+']
command+=['/reference:'+name for name in ['System.Windows.Forms.dll','System.Drawing.dll','System.Core.dll','System.IO.Compression.dll','System.IO.Compression.FileSystem.dll']]
command+=['/win32icon:'+str(root/'installer/assets/nexus-installer.ico'),'/resource:'+str(payload)+',payload.zip','/resource:'+str(root/'installer/assets/nexus-sidebar.png')+',nexus-sidebar.png','/resource:'+str(root/'installer/assets/nexus-installer.ico')+',nexus-installer.ico','/out:'+str(exe),str(root/'installer/Setup.cs'),str(root/'installer/Languages.cs')]
subprocess.run(command,check=True)
binary=out/'OBS-VST3-NEXUS-1.0-Windows-x64.zip'
with zipfile.ZipFile(binary,'w',zipfile.ZIP_DEFLATED) as z:
 for name,file in sorted(files.items()):z.write(file,'obs-vst3-nexus/'+name)
source=out/'OBS-VST3-NEXUS-1.0-Source.zip'
source_files=[root/name for name in ['CMakeLists.txt','README.md','LICENSE','LICENSES.md','THIRD-PARTY-NOTICES.md','.gitignore']]
for directory in ['docs','src','cmake','tests','third_party/iir1','third_party/libebur128','third_party/vst3sdk','docs/dependencies/vst3','installer/assets']:
 source_files += [f for f in (root/directory).rglob('*') if f.is_file() and not any(part in ['.git','__pycache__','build'] for part in f.relative_to(root).parts) and f.suffix.lower() not in ['.exe','.obj','.pdb']]
for name in ['scripts/build-release.ps1','scripts/package-release.py','scripts/generate-translations.py','installer/Setup.cs','installer/Languages.cs','docs/PLAN.md','docs/VALIDATION.md','docs/PUBLISHING.md','third_party/vst3sdk-nexus-unicode.patch','third_party/vst3sdk-NEXUS.md']:
 source_files.append(root/name)
with zipfile.ZipFile(source,'w',zipfile.ZIP_DEFLATED) as z:
 for file in sorted(set(source_files)):z.write(file,'OBS-VST3-NEXUS-1.0-Source/'+file.relative_to(root).as_posix())
for file in [binary,source]:
 with zipfile.ZipFile(file) as z:assert z.testzip() is None,file
manifest={file.name:hashlib.sha256(file.read_bytes()).hexdigest() for file in [exe,binary,source]}
(out/'SHA256SUMS.txt').write_text(''.join(value+'  '+name+'\n' for name,value in manifest.items()),encoding='utf-8')
(out/'package-manifest.json').write_text(json.dumps({'version':'1.0','payload':{name:hashlib.sha256(file.read_bytes()).hexdigest() for name,file in sorted(files.items())},'artifacts':manifest},indent=2),encoding='utf-8')
print(json.dumps({'version':'1.0','installer':str(exe),'binary_zip':str(binary),'source_zip':str(source),'payload_files':len(files)},indent=2))
