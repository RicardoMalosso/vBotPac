# Direct attachment

Run `run.cmd` on Windows with Python 3.12 and Visual Studio C++ x64 Build Tools installed. One matching open client is selected automatically; multiple matches prompt for a PID. `run.cmd --pid 21492` selects explicitly. No broker launch URL or separately running OtClientBot host is needed.

The launcher installs its pinned private OTCv8 UI runtime and CMake 3.31.6 under this application, builds the vendored PInj controller/bridge into `build-external`, revalidates the selected executable fingerprint, and attaches to that process. The controller injects the DLL only when its pipe is absent. A loaded DLL is pinned: after updating native code, restart the game once. Existing working DLL paths can be supplied with `--controller PATH --dll PATH` (both required). `--offline` retains the separate full-profile window without attachment.

The native sources, transport and pinned CMake helper come from RicardoMalosso/PInj revision `6c0967cfd5b64a9871609a42ddd2958919036b36`. They retain its profile validation, owning-Lua-thread execution, user-only named pipe and overlapped pipe I/O. This revision supports the included Exoria74 GL x64 fingerprint; other executable builds require matching verified profiles/native headers.

## Runtime boundary

The vBot window keeps a private UI LuaJIT. Game calls run inside the selected client's existing LuaJIT through `exec` requests, not through the old read-only HTTP snapshot broker. Game functions and userdata stay in that VM; session-scoped references cross the boundary. `g_game`, `g_map`, `g_things` and game object classes are exposed to the external context through proxies. Local UI/resources/config stay local. Trusted scripts can use `clientLua("return ...")` for a custom game-side expression, or `clientGlobal("name")` for a fork-specific global API. The original profile scripts are unchanged. The launcher services synchronous file-mailbox RPC outside the client and does not require an HTTP URL.

The external macro runner is enabled when connected and can be paused with its enable control. Enabled original macros run; their individual enable controls still apply. Client game/object callbacks are queued (256 events), polled and passed to the original executor callbacks. Overflow stops automation. This is direct trusted Lua API access, not the OtClientBot typed-action/priority broker: original vBot arbitration applies. Do not run both hosts against one client simultaneously.

Calls are serialized. UI request sequence, expiry and session tokens reject replay; uncertain transport failures close the controller and never retry a mutation. Closing the window disconnects event handlers and drops references. After an abnormal host exit, an inactive attachment can be replaced after five seconds. No automatic reconnection resumes macros. The reference registry is bounded to 8192 objects; copied results are bounded to 4096 entries and eight levels. Lua callback arguments cannot be transported as arbitrary executable closures; supported game callbacks use the event adapter. Missing fork APIs raise explicit errors.

## Verification

Seven offline tests use two real LuaJIT 2.0 VMs to exercise the UI facade, IPC mailbox, selected game environment, object method calls, mutation, Unicode/quoting, callbacks, attachment replacement, expiry, replay rejection and uncertain transport shutdown. GitHub Actions runs these on Windows/Linux and compiles the actual Windows x64 controller and DLL.

These tests do not establish full live vBot feature compatibility, game FPS or actual injection into Exoria. Linux UI smoke launch was attempted in the workspace but no X11 display was available. Startup and live testing on the user's Windows client remain necessary, especially item widgets/native userdata conversion and fork-specific UI/module methods. This change supplies direct attachment and the API/event transport; it does not claim every vBot module has passed live testing.

Published initial attachment: `7622e5c1b5e24e5720673319f37c9667fa30be86`. [CI 44417721066](https://github.com/RicardoMalosso/vBotPac/actions/runs/44417721066) passes six RPC tests on Windows/Linux and the actual native Windows build. A follow-up adds a seventh lifecycle fixture verifying macro pause/resume without invoking the disconnected UI client’s old refresh handler.
