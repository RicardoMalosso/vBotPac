"""Exercise the actual C++ compiler closure against LuaJIT's exported C API."""
import ctypes
from pathlib import Path
import sys
import tempfile
import lupa.luajit20 as luajit


def verify(library,lua_library=None):
    lua=ctypes.CDLL(str(Path(lua_library).resolve()) if lua_library else luajit.__file__);pointer=ctypes.c_void_p
    for name,restype,args in [('luaL_newstate',pointer,[]),('luaL_openlibs',None,[pointer]),('lua_close',None,[pointer]),('lua_gettop',ctypes.c_int,[pointer]),('luaL_loadbuffer',ctypes.c_int,[pointer,ctypes.c_char_p,ctypes.c_size_t,ctypes.c_char_p]),('lua_pcall',ctypes.c_int,[pointer,ctypes.c_int,ctypes.c_int,ctypes.c_int]),('lua_tolstring',ctypes.c_char_p,[pointer,ctypes.c_int,pointer])]:
        fn=getattr(lua,name);fn.restype=restype;fn.argtypes=args
    class Api(ctypes.Structure):_fields_=[(n,pointer) for n in ('load','pcall','gettop','settop','tolstring')]
    api=Api(*(ctypes.cast(getattr(lua,n),pointer).value for n in ('luaL_loadbuffer','lua_pcall','lua_gettop','lua_settop','lua_tolstring')))
    names=('pcall','gettop','settop','tolstring','getfield','type','toboolean','tonumber','pushinteger','pushvalue','pushcclosure','checkstack','createtable','setfield','pushboolean','pushlstring','rawget')
    class Stack(ctypes.Structure):_fields_=[(n,pointer) for n in names]
    stack=Stack(*(ctypes.cast(getattr(lua,'lua_'+n),pointer).value for n in names))
    native=ctypes.CDLL(str(Path(library).resolve()))
    native.module_test.argtypes=[ctypes.POINTER(Api),ctypes.POINTER(Stack),pointer,ctypes.c_char_p,pointer,ctypes.c_size_t]
    native.module_test.restype=ctypes.c_int
    L=lua.luaL_newstate();lua.luaL_openlibs(L)
    def run(source):
        top=lua.lua_gettop(L);out=ctypes.create_string_buffer(1048580)
        assert native.module_test(ctypes.byref(api),ctypes.byref(stack),L,source.encode(),out,len(out))==0
        assert lua.lua_gettop(L)==top,'Native compiler changed the caller stack'
        return out.value.decode()
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'inclient'))
    from launch import literal
    try:
        with tempfile.TemporaryDirectory(prefix='vbot-files-\u00e1 space-') as folder, tempfile.TemporaryDirectory(prefix='vbot-other-') as other:
            prefix='local _,open=...;'
            assert run(prefix+'files=assert(open('+literal(folder)+"));assert(files('mkdir','/storage'));assert(files('write','/storage/a',string.char(0,255)..'hello'));assert(files('read','/storage/a')==string.char(0,255)..'hello');assert(files('write','/storage/a','replaced'));assert(files('append','/storage/a','!'));assert(files('read','/storage/a')=='replaced!');return 'private binary IO'")=='OK\nprivate binary IO'
            assert run(prefix+'otherFiles=assert(open('+literal(other)+"));assert(not otherFiles('exists','/storage/a'));assert(files('read','/storage/a')=='replaced!');assert(files('list','/storage')=='a'..string.char(0));return 'root closures isolated'")=='OK\nroot closures isolated'
            assert run("assert(not files('write','/../escape','x'));assert(not files('read','C:/escape'));assert(not files('remove','/'));assert(not files('read','/missing'));return 'paths guarded'")=='OK\npaths guarded'
            if sys.platform!='win32':
                (Path(folder)/'outside').symlink_to(other,target_is_directory=True)
                assert run("assert(not files('write','/outside/escape','x'));return 'symlink guarded'")=='OK\nsymlink guarded'
            assert not (Path(other)/'escape').exists()
            print('PASS private native file adapter: atomic replacement, binary/Unicode paths, root isolation, traversal and symlink guards')
        assert run("local native=...;load=function()error('wrapped load called')end;loadstring=load;local f=assert(native(\"return 'CaveBot native load'\",'@vBot/main.lua'));return f()")=='OK\nCaveBot native load'
        assert run("local native=...;g_game={walk=function(dir)steps=(steps or 0)+dir end};local env=setmetatable({},{__index=_G});local f=assert(native('g_game.walk(2);return _G.g_game==g_game','@module/action.lua'));setfenv(f,env);assert(f());return steps")=='OK\n2'
        assert run("local native=...;local f,e=native('return )','@bad.lua');assert(not f and type(e)=='string');return 'syntax failure preserved'")=='OK\nsyntax failure preserved'
        assert run("local native=...;local f,e=native(42);assert(not f and e:find('string'));return 'type guarded'")=='OK\ntype guarded'
        assert run("local native=...;local f,e=native(string.rep(' ',1048577));assert(not f and e:find('1 MiB'));return 'bounded'")=='OK\nbounded'
        assert run("return )").startswith('ERROR\n')
        from package import files
        count=0
        for file,relative in files():
            if file.suffix!='.lua':continue
            result=run('local compile=...;local f,e=compile('+literal(file.read_text(encoding='utf-8'))+','+literal('@'+relative)+");assert(f,e);return 'compiled'")
            assert result=='OK\ncompiled',(relative,result)
            count+=1
        print('PASS native syntax compilation of',count,'complete runtime/profile Lua files')
    finally:lua.lua_close(L)
    print('PASS native compiler closure: actual LuaJIT C API, real _G/game calls, private nested compilation, syntax/type/size errors and stack restoration')

if __name__=='__main__':verify(*sys.argv[1:])
