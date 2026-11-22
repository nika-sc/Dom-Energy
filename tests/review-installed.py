"""Read-only checks of local installed copies, syntax and PowerShell encoding."""
import ast
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1]
files=list((root/'pc').glob('*.py'))+list((root/'tests').glob('*.py'))+list((root/'firmware/cydv4/web').glob('*.py'))
for path in files:ast.parse(path.read_text(encoding='utf-8-sig'),filename=str(path))
print('PASS: Python AST parse',len(files),'scripts',flush=True)
for name in ('DomMonitor.ps1','MetricHistory.ps1'):
    assert (root/'pc'/name).read_bytes().startswith(b'\xef\xbb\xbf'),name+' needs UTF-8 BOM for Windows PowerShell 5.1'
print('PASS: Russian WinForms scripts retain UTF-8 BOM',flush=True)
checks={}
for name in ('DomMonitor.ps1','MetricHistory.ps1','ControlAuth.ps1','flash-http.ps1','archive-run.ps1','archive-sd.py','board_http.py','DomMonitor-run.ps1'):
    path=root/'pc'/name;installed=Path('C:/esp32/tools')/name
    assert path.read_bytes()==installed.read_bytes(),name+' differs from installed'
    checks['pc/'+name]=hashlib.sha256(path.read_bytes()).hexdigest()
for path in (root/'firmware/cydv4').rglob('*'):
    if path.is_file() and path.name!='secrets.example.h' and path.suffix in {'.h','.ino','.js','.html','.css','.py'}:
        relative=path.relative_to(root/'firmware/cydv4');installed=Path('C:/esp32/cydv4')/relative
        assert installed.exists() and path.read_bytes()==installed.read_bytes(),'Firmware differs: '+str(relative)
        checks['firmware/cydv4/'+relative.as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
print('PASS: firmware/web and installed Windows source hashes match',flush=True)
destination=root/'pc/firmware-archive/final-validation-20261113/installed-source-hashes.json'
destination.write_text(json.dumps(checks,indent=2),encoding='utf-8')
