# vBotPac — in-client branch

This branch loads the complete pinned OTCv8 `game_bot` runtime and vBot profile into a selected existing client's LuaJIT through the injected DLL. Gameplay uses actual game objects and `_G`.

```powershell
git fetch origin
git switch feat/in-client-vbot-module
git pull --ff-only
.\run.cmd
```

Python 3.12 and Visual Studio C++ x64 Build Tools are required. The launcher installs project-local pinned CMake, builds the controller/DLL, and selects a fingerprint-compatible PID. Use `run.cmd --pid 21492` to select explicitly. Restart the game once when updating to the private-files DLL; an older loaded bridge is rejected.

All runtime files, saved profiles, settings, traces and bot logs now live under **this repository's `data/inclient/` directory**. The launcher prints the exact trace/log paths. Existing profile data is copied from the previous client-write-directory layout on first use; the source and current private saves are preserved. No resource directory is mounted into the client. UI definitions load from memory, bundled images are embedded, and vBot styles use a private `VBPM_` prefix. Missing custom fonts use the client's inherited font, and existing child widgets are bound explicitly for fork compatibility.

vBot's `print` and logger calls write to its own log. Its settings use a private JSON file. Exoria's global logger/settings are untouched. Native client UI/game errors can still be recorded by Exoria itself; sharing its engine cannot guarantee zero native log entries. Existing log history is not edited.

No `.otmod` is installed, no OTClient module is registered, and existing `modules.game_bot` is unchanged. The ordinary native DLL remains visible to Windows. Keep the launcher open; commands are `status`, `pause`, `resume`, and `unload`. Ctrl+C or a lost heartbeat unloads the runtime.

See [architecture and verification](docs/inclient-module.md). Full live Exoria startup/gameplay remains to be verified; offline fixtures do not prove complete client compatibility.
