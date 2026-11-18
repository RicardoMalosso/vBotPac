"""Pinned, project-local CMake for the launcher, native harness and CI."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WINDOWS_GENERATOR = 'NMake Makefiles'
LINUX_GENERATOR = 'Unix Makefiles'


def venv_python():
    return ROOT / '.venv' / ('Scripts/python.exe' if os.name == 'nt' else 'bin/python')


def cmake_version():
    lock = (ROOT / 'requirements-cmake.txt').read_text(encoding='utf-8')
    pins = re.findall(r'^cmake==([0-9]+\.[0-9]+\.[0-9]+)\s', lock, re.MULTILINE)
    if len(pins) != 1:
        raise ValueError('requirements-cmake.txt must pin exactly one CMake version')
    return pins[0]


def cmake_environment():
    # Explicit -G/-A plus removal of ambient generator overrides makes ordinary
    # PowerShell and developer terminals select the same Windows build system.
    return {k: v for k, v in os.environ.items()
            if k.upper() not in {'CMAKE_GENERATOR', 'CMAKE_GENERATOR_PLATFORM',
                                 'CMAKE_GENERATOR_TOOLSET', 'CMAKE_GENERATOR_INSTANCE'}}


def native_build_environment(*, windows=None):
    """Load installed MSVC x64 tools without requiring a developer shell/VS year."""
    windows = os.name == 'nt' if windows is None else windows
    environment = cmake_environment()
    if not windows:
        return environment
    names = {key.upper(): value for key, value in environment.items()}
    candidates = [Path(names[name]) / 'Microsoft Visual Studio/Installer/vswhere.exe'
                  for name in ('PROGRAMFILES(X86)', 'PROGRAMFILES') if names.get(name)]
    finder = next((path for path in candidates if path.is_file()), None)
    if finder is None:
        raise ValueError('MSVC discovery missing. Install Visual Studio Build Tools with Desktop development with C++')
    located = subprocess.run([str(finder), '-latest', '-prerelease', '-products', '*',
                              '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                              '-property', 'installationPath', '-utf8'], env=environment,
                             capture_output=True, text=True, encoding='utf-8', timeout=30)
    if located.returncode or not located.stdout.strip():
        raise ValueError('No Visual Studio installation with MSVC x64 tools was found')
    installation = Path(located.stdout.strip()).resolve()
    script = installation / 'Common7/Tools/VsDevCmd.bat'
    if not script.is_file():
        raise ValueError('MSVC developer environment is missing: ' + str(script))
    command = Path(names.get('SYSTEMROOT', 'C:/Windows')) / 'System32/cmd.exe'
    # /u makes SET output UTF-16LE, preserving Unicode paths. Keep the copied
    # environment private: reports contain commands/results, never this output.
    # CMD parses quotes differently from MSVCRT; passing a list would add
    # backslash escapes around the batch path and break installations in spaces.
    shell_command = ('"' + str(command) + '" /d /u /s /c ""' + str(script)
                     + '" -no_logo -arch=x64 -host_arch=x64 >nul && set"')
    shell = subprocess.run(shell_command,
                           env=environment, capture_output=True, timeout=30)
    if shell.returncode:
        raise ValueError('Could not initialize the MSVC x64 developer environment')
    loaded = {}
    for line in shell.stdout.decode('utf-16le', errors='replace').splitlines():
        key, separator, value = line.partition('=')
        if separator and key:
            loaded[key.upper()] = value
    compiler = shutil.which('cl.exe', path=loaded.get('PATH', ''))
    make = shutil.which('nmake.exe', path=loaded.get('PATH', ''))
    if (loaded.get('VSCMD_ARG_TGT_ARCH', '').lower() != 'x64' or not compiler or not make
            or not Path(compiler).resolve().is_relative_to(installation)
            or not Path(make).resolve().is_relative_to(installation)):
        raise ValueError('The selected Visual Studio installation did not provide MSVC/NMake x64 tools')
    loaded['CC'] = loaded['CXX'] = compiler
    return {key: value for key, value in loaded.items() if not key.startswith('CMAKE_GENERATOR')}


def _run(command, timeout=30):
    result = subprocess.run(command, cwd=ROOT, env=cmake_environment(),
                            capture_output=True, text=True, timeout=timeout)
    return {'command': command, 'exit_code': result.returncode,
            'stdout': result.stdout[-16000:], 'stderr': result.stderr[-16000:]}


def _probe_cmake():
    # -I ignores PYTHONPATH, the user site and the current directory. Query the
    # venv package for its *native* binary; do not invoke a PATH-selected wrapper.
    query = ("import cmake,json,os; "
             "print(json.dumps({'version':cmake.__version__,"
             "'binary':os.path.join(cmake.CMAKE_BIN_DIR,"
             "'cmake.exe' if os.name=='nt' else 'cmake')}))")
    try:
        probe = _run([str(venv_python()), '-I', '-c', query])
        if probe['exit_code']:
            return None
        data = json.loads(probe['stdout'])
        binary = Path(data['binary']).resolve()
        if (data['version'] != cmake_version() or not binary.is_file()
                or not binary.is_relative_to((ROOT / '.venv').resolve())):
            return None
        version = _run([str(binary), '--version'])
        if (version['exit_code'] or not version['stdout'].splitlines()
                or version['stdout'].splitlines()[0] != 'cmake version ' + cmake_version()):
            return None
        return str(binary), version
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired):
        return None


def cmake_executable(checks=None):
    """Verify the local binary, repairing drift once from hash-locked wheels."""
    if not venv_python().is_file():
        raise ValueError('Project Python missing. Run: python scripts/dev.py bootstrap')
    found = _probe_cmake()
    if found is None:
        install = _run([str(venv_python()), '-I', '-m', 'pip', '--isolated', 'install',
                        '--require-hashes', '--only-binary=:all:', '--no-deps',
                        '--force-reinstall', '-r', str(ROOT / 'requirements-cmake.txt')], 180)
        if checks is not None:
            checks.append(install)
        if install['exit_code']:
            raise ValueError('Cannot install pinned project CMake: ' + install['stderr'])
        found = _probe_cmake()
        if found is None:
            raise ValueError('Project CMake version/binary validation failed after installation')
    binary, version = found
    if checks is not None:
        checks.append(version)
    return binary


def _cache_values(build):
    cache = build / 'CMakeCache.txt'
    if not cache.is_file():
        return None
    values = {}
    for line in cache.read_text(encoding='utf-8', errors='replace').splitlines():
        if not line or line.startswith(('#', '//')) or '=' not in line:
            continue
        key, value = line.split('=', 1)
        values[key.split(':', 1)[0]] = value
    return values


def configure_command(cmake, source, build, *, windows=None, profile_header=None, environment=None):
    """Use the supported generator and refresh only incompatible build caches."""
    windows = os.name == 'nt' if windows is None else windows
    source = (ROOT / source).resolve()
    build = (ROOT / build).resolve()
    if source == build:
        raise ValueError('Use a separate build directory, not the source directory')
    generator = WINDOWS_GENERATOR if windows else LINUX_GENERATOR
    platform = ''
    command = [cmake, '-S', str(source), '-B', str(build), '-G', generator]
    if windows:
        command += ['-DCMAKE_BUILD_TYPE=Release',
                    '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=' + (build / 'Release').as_posix()]
        if environment is not None:
            for language in ('C', 'CXX'):
                command.append('-DCMAKE_' + language + '_COMPILER=' + Path(environment['CC' if language == 'C' else 'CXX']).as_posix())
            command.append('-DCMAKE_MAKE_PROGRAM=' + Path(shutil.which('nmake.exe', path=environment['PATH'])).as_posix())
    if profile_header is not None:
        header = (ROOT / profile_header).resolve()
        if not header.is_file():
            raise ValueError('Generated profile header does not exist: ' + str(header))
        command.append('-DPINJ_PROFILE_HEADER=' + header.as_posix())
    cached = _cache_values(build)
    if cached is not None:
        expected = {'CMAKE_GENERATOR': generator, 'CMAKE_GENERATOR_PLATFORM': platform,
                    'CMAKE_CACHE_MAJOR_VERSION': cmake_version().split('.')[0],
                    'CMAKE_CACHE_MINOR_VERSION': cmake_version().split('.')[1],
                    'CMAKE_CACHE_PATCH_VERSION': cmake_version().split('.')[2]}
        changed = any(cached.get(key) != value for key, value in expected.items())
        paths = [('CMAKE_COMMAND', cmake), ('CMAKE_HOME_DIRECTORY', source)]
        if windows and environment is not None:
            paths += [('CMAKE_C_COMPILER', environment['CC']), ('CMAKE_CXX_COMPILER', environment['CXX']),
                      ('CMAKE_MAKE_PROGRAM', shutil.which('nmake.exe', path=environment['PATH']))]
        for key, path in paths:
            changed |= os.path.normcase(cached.get(key, '').replace('\\', '/')) != os.path.normcase(str(path).replace('\\', '/'))
        if changed:
            # CMake's supported migration removes only its own cache/metadata;
            # keep build-external/Release paths and never recursively delete builds.
            command.append('--fresh')
    return command
