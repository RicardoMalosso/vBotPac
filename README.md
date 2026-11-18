# vBotPac

Original vBot profile in a separate OTCv8 window, with direct attachment to a running OTClient.

On Windows, install Python 3.12 and Visual Studio C++ x64 Build Tools, then run:

```powershell
.\run.cmd
```

Choose a PID if several compatible clients are open. The launcher builds the bridge with an application-local pinned CMake, injects it into the selected client and opens the vBot window. No broker URL is requested. Game API calls use that client's existing LuaJIT; the separate window retains its UI runtime.

```powershell
.\run.cmd --pid 21492
.\run.cmd --offline
```

Currently includes the verified Exoria74 GL x64 executable profile. Other builds need matching native compatibility data. Updating an already loaded bridge requires one game restart.

See [direct attachment, architecture and verification](docs/direct-attachment.md). The transport has offline two-VM tests and Windows build CI; full live Exoria/vBot feature compatibility remains to be verified.
