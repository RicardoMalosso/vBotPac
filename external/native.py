"""Build/reuse the same profile-checked x64 bridge used by OtClientBot."""
import os
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid
from project_cmake import cmake_executable, cmake_version, configure_command, native_build_environment, venv_python
ROOT = Path(__file__).resolve().parents[1]


def source_key(environment):
    digest = hashlib.sha256()
    inputs = [ROOT/'CMakeLists.txt', ROOT/'requirements-cmake.txt',
              ROOT/'external/native.py', ROOT/'external/project_cmake.py']
    inputs += sorted(p for p in (ROOT/'native').rglob('*') if p.suffix in {'.cpp', '.hpp', '.h'})
    for path in inputs:
        digest.update(path.relative_to(ROOT).as_posix().encode('utf-8') + b'\0')
        digest.update(path.read_bytes())
    digest.update(json.dumps({'cmake': cmake_version(), 'compiler': environment.get('CXX'),
                              'configuration': 'Release-x64'}, sort_keys=True).encode('utf-8'))
    return digest.hexdigest()


def cached_build(cmake, environment):
    """Reuse verified outputs; every rebuild gets a new directory, even after failure."""
    cache = ROOT/'build-external'
    key = source_key(environment)
    manifest = cache/'cache'/(key+'.json')
    try:
        saved = json.loads(manifest.read_text(encoding='utf-8'))
        paths = tuple((cache/entry['path']).resolve() for entry in saved['products'])
        if (saved['key'] == key and len(paths) == 2
                and paths[0].name == 'pinj.exe' and paths[1].name == 'pinj_bridge.dll'
                and all(path.is_relative_to(cache.resolve()) and path.is_file()
                        and hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256']
                        for path,entry in zip(paths,saved['products']))):
            return paths
    except (OSError, ValueError, KeyError, TypeError):
        pass
    # A mapped Windows EXE/DLL cannot be replaced. Never configure or relink
    # an existing output directory, and never delete old loaded versions.
    build = cache/'versions'/key[:16]/uuid.uuid4().hex[:12]
    build.mkdir(parents=True)
    log = build/'build.log'
    print('Building native bridge '+key[:12]+'; log: '+str(log), flush=True)
    commands = [configure_command(cmake, str(ROOT), str(build), environment=environment),
                [cmake, '--build', str(build), '--config', 'Release']]
    try:
        with log.open('wb') as output:
            for command in commands:
                subprocess.run(command, cwd=ROOT, env=environment, stdout=output,
                               stderr=subprocess.STDOUT, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        code = getattr(error, 'returncode', 'unavailable')
        raise ValueError('Native build failed (exit '+str(code)+'). See '+str(log)) from None
    paths = build/'pinj.exe', build/'pinj_bridge.dll'
    if not all(path.is_file() for path in paths):
        paths = build/'Release/pinj.exe', build/'Release/pinj_bridge.dll'
    if not all(path.is_file() for path in paths):
        raise ValueError('Native build did not produce both binaries. See '+str(log))
    saved = {'key': key, 'products': [
        {'path': path.relative_to(cache).as_posix(),
         'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for path in paths]}
    manifest.parent.mkdir(parents=True, exist_ok=True)
    temporary = manifest.with_name(manifest.name+'.'+uuid.uuid4().hex+'.tmp')
    temporary.write_text(json.dumps(saved), encoding='utf-8')
    temporary.replace(manifest)
    return paths


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
                raise ValueError('Launch with Python 3.12: py -3.12 inclient/launch.py')
            try:
                subprocess.run([sys.executable, '-m', 'venv', str(ROOT/'.venv')], check=True)
            except subprocess.CalledProcessError as error:
                raise ValueError('Cannot create project Python environment (exit '+str(error.returncode)+')') from None
        cmake = cmake_executable()
        env = native_build_environment()
        paths = cached_build(cmake, env)
    if not all(p.is_file() for p in paths):
        raise ValueError('Controller executable and bridge DLL must both exist')
    return paths
