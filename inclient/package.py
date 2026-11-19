"""Stage the complete game_bot runtime and profile under project-owned data."""
import hashlib
import base64
from pathlib import Path
import re
import shutil
ROOT=Path(__file__).resolve().parents[1]


def adapt(relative, text, runtime, profiles):
    text=text.replace('/bot/',profiles+'/')
    text=text.replace('/modules/game_bot/',runtime+'/')
    if relative.endswith('.otui'):
        text=text.replace('&save: true','&save: false')
        if relative=='edit.otui':
            encoded=base64.b64encode((ROOT/'vendor/game_bot/configs.png').read_bytes()).decode('ascii')
            text=text.replace('image-source: configs.png','image-source-base64: '+encoded)
        text=text.replace('modules.game_bot.','_G.__vbotpac_module.scope.')
        if relative=='bot.otui':text=text.replace('id: botWindow','id: vbotpacModuleWindow')
        if relative=='edit.otui':text=text.replace('id: editWindow','id: vbotpacModuleEditor')
    if relative=='executor.lua':
        text=text.replace('local context = {}','local context = setmetatable({}, {__index=getfenv(1)})')
        text=text.replace('context.math = math','context.math = setmetatable({mod=math.mod or math.fmod},{__index=math})')
        text=text.replace('G.botContext = nil','if onContextReady then onContextReady(context) end\n  G.botContext = nil')
    if relative=='functions/ui_legacy.lua':
        text=text.replace("tab:setFont('small-9px')","if g_fonts and g_fonts.fontExists and g_fonts.fontExists('small-9px') then tab:setFont('small-9px') end")
    if relative=='bot.lua':
        text=text.replace("'botButton'","'vbotpacModuleButton'")
        text=text.replace('if widget.botWidget then','if widget.botWidget and ownsWidget(widget) then').replace('if child.botWidget then','if child.botWidget and ownsWidget(child) then')
        text=text.replace('g_game.enableTileThingLuaCallback(false)','-- Leave the shared client callback setting unchanged')
        text=text.replace('if not modules.client_profiles.ChangedProfile then','if not modules.client_profiles or not modules.client_profiles.ChangedProfile then')
        text+='''
function vbpIsRunning()return botExecutor~=nil end
function vbpSetEnabled(value)
 local settings=g_settings.getNode('bot') or {}
 local index=g_game.getCharacterName()..'_'..g_game.getClientVersion()
 settings[index]=settings[index] or {config='vBotPac'};settings[index].enabled=value
 g_settings.setNode('bot',settings);refresh()
end
'''
    return text


def files():
    roots=[(ROOT/'vendor/game_bot','runtime')]+[(ROOT/n,'profile/vBotPac/'+n) for n in ('vBot','cavebot','targetbot')]
    result=[(ROOT/'_Loader.lua','profile/vBotPac/_Loader.lua')]
    for folder,prefix in roots:
        for p in sorted(folder.rglob('*')):
            if p.is_file() and p.suffix!='.otmod':
                if p.is_symlink() or not p.resolve().is_relative_to(folder.resolve()):raise ValueError('Symlink resources unsupported')
                result.append((p,prefix+'/'+p.relative_to(folder).as_posix()))
    return result


def stage(write_dir, selected, character, legacy_dir=None):
    write_dir=Path(write_dir).resolve()
    write_dir.mkdir(parents=True,exist_ok=True)
    sources=files();digest=hashlib.sha256(b'vbotpac-private-resources-v3\n')
    for source,relative in sources:digest.update(relative.encode());digest.update(source.read_bytes())
    digest.update((ROOT/'inclient/bootstrap.lua').read_bytes())
    revision=digest.hexdigest()[:16]
    identity=hashlib.sha256((selected['sha256']+'\n'+character).encode()).hexdigest()[:24]
    root=write_dir/'vbotpac_inclient'
    if root.is_symlink():raise ValueError('Symlink runtime destination unsupported')
    # Retain the compatible virtual naming scheme. These files now live only
    # under this project's data root; UI uses strings, not native file loading.
    runtime='/vbotpac_inclient/bot/runtime/'+revision
    profiles='/vbotpac_inclient/profiles/'+identity+'/bot'
    if legacy_dir:
        previous=Path(legacy_dir).resolve()/profiles.lstrip('/')
        if previous.is_dir() and previous.resolve().is_relative_to(Path(legacy_dir).resolve()):
            for old in previous.rglob('*'):
                if old.is_symlink():raise ValueError('Symlink legacy resource unsupported')
                if old.is_file():
                    target=write_dir/profiles.lstrip('/')/old.relative_to(previous)
                    if not target.exists():
                        target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(old,target)
    helpers={name:[] for name in ('functions','panels')}
    styles={}
    for source,relative in sources:
        if relative.startswith('runtime/'):
            name=relative[8:];virtual=runtime+'/'+name
            for folder in helpers:
                if name.startswith(folder+'/') and name.endswith('.lua'):helpers[folder].append(name)
        else:
            name=relative[8:];virtual=profiles+'/'+name
        destination=write_dir/virtual.lstrip('/')
        # Never follow an unexpected existing symlink into unrelated files.
        current=destination
        while current!=write_dir:
            if current.is_symlink():raise ValueError('Symlink resource destination unsupported')
            current=current.parent
        destination.parent.mkdir(parents=True,exist_ok=True)
        if source.suffix in ('.lua','.otui'):
            text=source.read_text(encoding='utf-8')
            if source.suffix=='.otui':
                for style in re.findall(r'^#?([A-Za-z_][\w]*)\s*<\s*[\w]+\s*$',text,re.MULTILINE):styles[style]=virtual
            name=relative[8:] if relative.startswith('runtime/') else relative
            content=adapt(name,text,runtime,profiles).encode('utf-8')
        else:content=source.read_bytes()
        temporary=destination.with_name(destination.name+'.vbp-tmp');temporary.write_bytes(content);temporary.replace(destination)
    return dict(runtime=runtime,profile=profiles,helpers=helpers,styles=styles,
                dataRoot=str(write_dir),settings='/vbotpac_inclient/profiles/'+identity+'/settings.json',
                log='/vbotpac_inclient/logs/inclient.log',
                trace='/vbotpac_inclient/profiles/'+identity+'/startup-trace.json')
