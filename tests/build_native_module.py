import argparse
import os
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'external'))
from project_cmake import cmake_executable,configure_command,native_build_environment
parser=argparse.ArgumentParser(description='Build the native compiler harness; CI supplies a pinned standalone LuaJIT source checkout.')
parser.add_argument('--lua-source',type=Path)
args=parser.parse_args()
cmake=cmake_executable();env=native_build_environment();build=ROOT/'build-module-test'
lua_library=None
if args.lua_source:
    source=args.lua_source.resolve()
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
    if revision!='b83bcba6ea300d4eb16e8257ddf80bcdef1f938b':
        raise ValueError('The native harness requires its pinned LuaJIT source revision')
    if os.name=='nt':
        subprocess.run([env.get('COMSPEC','cmd.exe'),'/d','/c','msvcbuild.bat'],cwd=source/'src',env=env,check=True)
        lua_library=source/'src/lua51.dll'
    else:
        subprocess.run(['make','-j2'],cwd=source,env=env,check=True)
        lua_library=source/'src/libluajit.so'
    if not lua_library.is_file():raise ValueError('Standalone test LuaJIT library was not built')
subprocess.run(configure_command(cmake,str(ROOT/'tests/native'),str(build),environment=env),cwd=ROOT,env=env,check=True)
subprocess.run([cmake,'--build',str(build),'--config','Release'],cwd=ROOT,env=env,check=True)
files=list(build.rglob('module_test.dll'))+list(build.rglob('libmodule_test.so'))
if len(files)!=1:raise ValueError('Native compiler test library missing or ambiguous')
subprocess.run([sys.executable,str(ROOT/'tests/verify_native_module.py'),str(files[0])]+([str(lua_library)] if lua_library else []),cwd=ROOT,check=True)
