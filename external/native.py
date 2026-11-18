"""Build/reuse the same profile-checked x64 bridge used by OtClientBot."""
import os
from pathlib import Path
import subprocess
import sys
from project_cmake import cmake_executable, configure_command, native_build_environment, venv_python
ROOT = Path(__file__).resolve().parents[1]


def binaries(controller=None, dll=None):
    if bool(controller) != bool(dll):
        raise ValueError('Supply both --controller and --dll')
    if controller:
        paths = Path(controller).resolve(), Path(dll).resolve()
    else:
        if os.name != 'nt':
            raise ValueError('Direct native attachment requires Windows x64')
        python = venv_python()
        if not python.exists():
            if sys.version_info[:2] != (3, 12):
                raise ValueError('Launch with Python 3.12: py -3.12 external/window.py')
            subprocess.run([sys.executable, '-m', 'venv', str(ROOT/'.venv')], check=True)
        cmake = cmake_executable()
        env = native_build_environment()
        build = ROOT/'build-external'
        subprocess.run(configure_command(cmake, str(ROOT), str(build), environment=env), cwd=ROOT, env=env, check=True)
        subprocess.run([cmake, '--build', str(build), '--config', 'Release'], cwd=ROOT, env=env, check=True)
        paths = build/'pinj.exe', build/'pinj_bridge.dll'
        if not paths[0].is_file():
            paths = build/'Release/pinj.exe', build/'Release/pinj_bridge.dll'
    if not all(p.is_file() for p in paths):
        raise ValueError('Controller executable and bridge DLL must both exist')
    return paths
