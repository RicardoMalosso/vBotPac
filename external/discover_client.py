"""Read-only Windows process discovery; executable names are not compatibility evidence."""
import hashlib
import json
import os
from pathlib import Path
import subprocess


def windows_processes():
    if os.name != 'nt':
        raise ValueError('Client discovery requires Windows')
    command = "$ErrorActionPreference = 'Stop'; [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false); @(@(Get-CimInstance Win32_Process | Select-Object ProcessId,ExecutablePath)) | ConvertTo-Json -Compress"
    result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command],
                            capture_output=True, timeout=20)
    if result.returncode:
        raise ValueError('Cannot enumerate Windows processes: ' + result.stderr.decode('utf-8', errors='replace')[-1000:])
    records = json.loads(result.stdout.decode('utf-8-sig'))
    return records if isinstance(records, list) else [records]


def find_clients(profile_path, processes=None):
    profile = json.loads(Path(profile_path).read_text(encoding='utf-8'))
    digest = profile['sha256']
    if len(digest) != 64 or any(c not in '0123456789abcdef' for c in digest):
        raise ValueError('Invalid profile SHA-256')
    records = windows_processes() if processes is None else processes
    cache, matches, inaccessible = {}, [], 0
    for process in records:
        pid, path = process.get('ProcessId'), process.get('ExecutablePath')
        if not path:
            inaccessible += 1
            continue
        try:
            if path not in cache:
                file = Path(path)
                if profile.get('size') is not None and file.stat().st_size != profile['size']:
                    cache[path] = False
                else:
                    with file.open('rb') as stream:
                        cache[path] = hashlib.file_digest(stream, 'sha256').hexdigest() == digest
            if cache[path] and type(pid) is int and pid > 0:
                matches.append({'pid': pid, 'target': path, 'sha256': digest})
        except OSError:
            inaccessible += 1
    matches.sort(key=lambda item: item['pid'])
    return {'matches': matches, 'inaccessible_processes': inaccessible}


def select_client(discovery, pid=None, target=None):
    matches = discovery['matches']
    if pid is not None:
        if pid <= 0:
            raise ValueError('Invalid PID')
        matches = [m for m in matches if m['pid'] == pid]
    if target is not None:
        expected = os.path.normcase(os.path.abspath(target))
        matches = [m for m in matches if os.path.normcase(os.path.abspath(m['target'])) == expected]
    if not matches:
        raise ValueError('No accessible running client matches the profile and supplied selectors. Start the client or check process access; discovery does not launch it.')
    if len(matches) != 1:
        raise ValueError('Multiple matching clients: ' + ', '.join(str(m['pid']) for m in matches) + '. Select one with --pid.')
    return matches[0]

