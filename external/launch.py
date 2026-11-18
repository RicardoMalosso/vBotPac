"""Install the pinned private UI runtime and open the original vBot profile."""
import argparse
import hashlib
import getpass
import json
import uuid
from broker import Broker
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from urllib.request import urlopen
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
REV = "aacfe3f1fe4bcadb5a34ff1f36263e1c96b3dd32"
DIGEST = "b06b4a9f068dfdb5eeec7de95f2a18fd6f866b27dddba453ed55a94b63e0e419"
INSTALL = ROOT / ".vbot-runtime" / REV


def install(archive):
    if INSTALL.is_dir():
        return
    INSTALL.parent.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=INSTALL.parent) as stage:
        if archive is None:
            archive = Path(stage) / "runtime.zip"
            with urlopen("https://codeload.github.com/OTCv8/otclientv8/zip/" + REV,
                         timeout=120) as src, archive.open("wb") as dest:
                shutil.copyfileobj(src, dest)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != DIGEST:
            raise ValueError("Runtime archive checksum mismatch")
        with ZipFile(archive) as bundle:
            bundle.extractall(stage)
        runtime = Path(stage) / ("otclientv8-" + REV)
        for relative in ("init.lua", "modules/game_bot/bot.lua", "modules/game_bot/executor.lua"):
            script = runtime / relative
            shutil.copyfile(script, script.with_suffix(".upstream"))
        runtime.rename(INSTALL)


def configure(snapshot_name=None):
    profile = INSTALL / "bot/vBotPac"
    profile.mkdir(parents=True, exist_ok=True)
    for name in ("cavebot", "targetbot", "vBot"):
        dest = profile / name
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(ROOT / name, dest)
    shutil.copyfile(ROOT / "_Loader.lua", profile / "_Loader.lua")
    mod = INSTALL / "modules/game_bot"
    init = (INSTALL / "init.upstream").read_text(encoding="utf-8")
    init = init.replace('APP_NAME = "otclientv8"', 'APP_NAME = "vbotpac"')
    init = init.replace('g_app.setName("OTCv8")', 'Services = {}\ng_app.setName("vBotPac")')
    init += '\ng_modules.ensureModuleLoaded("game_bot")\nscheduleEvent(modules.game_bot.openExternalProfile, 350)\n'
    (INSTALL / "init.lua").write_text(init, encoding="utf-8")
    host = (ROOT / "external/host.lua").read_text(encoding="utf-8")
    host = host.replace("local snapshotPath = nil", "local snapshotPath = " + (json.dumps("/" + snapshot_name) if snapshot_name else "nil"))
    (mod / "bot.lua").write_text((mod / "bot.upstream").read_text(encoding="utf-8") + "\n" + host,
                                 encoding="utf-8")
    executor = (mod / "executor.upstream").read_text(encoding="utf-8")
    executor = executor.replace("context.math = math", "context.math = setmetatable({mod = math.mod or math.fmod}, {__index = math})")
    executor = executor.replace("context.player = g_game.getLocalPlayer()", """
  -- Offline startup observations. No game actions run during profile loading.
  context.player = {
    getName = function() return externalSnapshot.name or "Offline" end,
    getPosition = function() return externalSnapshot.position or {x=0, y=0, z=7} end,
    getVocation = function() return 0 end,
    getFreeCapacity = function() return 0 end,
    getExperience = function() return 0 end,
    getDirection = function() return 0 end,
    getBlessings = function() return 0 end,
    getLevel = function() return 1 end
  }""")
    executor = executor.replace("context.dofile = function(file) assert",
                                'context.dofile = function(file) g_logger.info("[vBotPac] load " .. file); assert')
    (mod / "executor.lua").write_text(executor, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Install from a cached upstream ZIP")
    parser.add_argument("--connect", action="store_true", help="Connect to a running broker")
    parser.add_argument("--pid", type=int, help="Require this broker-selected client PID")
    args = parser.parse_args()
    broker = None
    if args.connect or args.pid is not None:
        broker = Broker(getpass.getpass("Paste the broker launch URL (hidden): "), args.pid)
        initial = broker.read()
    install(args.archive)
    snapshot = INSTALL / ("session-" + uuid.uuid4().hex + ".json")
    def publish(value):
        temporary = snapshot.with_suffix(".tmp")
        temporary.write_text(json.dumps(value), encoding="utf-8")
        temporary.replace(snapshot)
    configure(snapshot.name if broker else None)
    binary = INSTALL / ("otclient_gl.exe" if sys.platform == "win32" else "otclient_linux")
    binary.chmod(binary.stat().st_mode | 0o100)
    if not broker:
        return subprocess.call([str(binary)], cwd=INSTALL)
    try:
        publish(initial)
        with subprocess.Popen([str(binary)], cwd=INSTALL) as window:
            try:
                while True:
                    try:
                        return window.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        publish(broker.read())
            except (OSError, ValueError, KeyError) as error:
                publish({"error": str(error)})
                print(f"Broker disconnected: {error}", file=sys.stderr)
                return window.wait()
            finally:
                if window.poll() is None:
                    window.terminate()
    finally:
        snapshot.unlink(missing_ok=True)
        snapshot.with_suffix(".tmp").unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
