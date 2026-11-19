# vBotPac — in-client branch

This branch loads vBot directly into an existing compatible client's LuaJIT through the injected DLL. The complete pinned OTCv8 `game_bot` runtime is bundled: host, executor, helper functions, panels, editor/configuration UI and assets. No separate OTCv8 window or proxy objects are used for gameplay.

```powershell
git fetch origin
git switch feat/in-client-vbot-module
.\run.cmd
```

Python 3.12 and Visual Studio C++ x64 Build Tools are required. The launcher installs a project-local pinned CMake, builds the controller/DLL, lists matching running clients, and lets you select a PID. `run.cmd --pid 21492` selects explicitly. After switching from a previously loaded DLL, restart the game once; an old bridge is rejected instead of silently falling back.

vBot appears as a normal in-game bot window. Its profile context inherits real client globals, including custom APIs, and shares the real game `_G`, userdata and event system. Its own variables live in a private Lua namespace. It does **not** register an OTClient module, install an `.otmod`, replace `modules.game_bot`, or filter the module list. The ordinary native DLL remains visible to Windows and debugging tools.

Keep the launcher open. Commands are `status`, `pause`, `resume`, and `unload`; Ctrl+C unloads. A lost host heartbeat also unloads the runtime. `game_bot` configuration and profiles are staged in the client's write directory, under `vbotpac_inclient`, separately from existing client bot data.

See [architecture, readiness checks and limitations](docs/inclient-module.md). Offline/native tests are not proof of actual Exoria compatibility: the launcher reports a full startup failure and writes a script trace if the client lacks required OTCv8 APIs or UI facilities. Full live acceptance remains to be established.
