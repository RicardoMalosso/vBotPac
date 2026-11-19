"""Windows integration: rebuild while the previous DLL is still mapped."""
import ctypes
import hashlib
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'external'))
from native import binaries

if os.name!='nt':raise SystemExit('This integration check requires Windows')
first=binaries()
original=first[1].read_bytes()
# The bridge rejects this Python process's executable fingerprint before
# installing any hook or IPC. Keep the image mapped to reproduce Windows locks.
held=ctypes.WinDLL(str(first[1]))
assert first==binaries(),'An unchanged source revision rebuilt a mapped DLL'
result=subprocess.run([str(first[0]),str(os.getpid()),str(first[1]),'--stdio'],
                      input=b'',capture_output=True,timeout=15)
assert result.returncode==1 and b'already loaded' in result.stderr,result.stderr
header=ROOT/'native/profile_config.hpp'
source=header.read_bytes()
try:
    header.write_bytes(source+b'\n// native build cache integration check\n')
    second=binaries()
    assert first!=second,'Changed native source reused a mapped output path'
    assert second[0].is_file() and second[1].is_file()
    assert hashlib.sha256(first[1].read_bytes()).digest()==hashlib.sha256(original).digest()
finally:
    header.write_bytes(source)
assert first==binaries(),'Restored source did not reuse its verified artifact'
print('PASS Windows mapped-DLL rebuild, verified cache reuse and duplicate-load rejection')
