# In-client vBot runtime

Branch: `feat/in-client-vbot-module`. The external implementation on `main` is unchanged.

## Runtime

The controller attaches to an explicitly selected, fingerprint-checked Windows x64 process. It binds the owning game Lua state with the existing session gate. The new `module-v1` command compiles the bootstrap on that same owning thread and supplies a private C compiler closure. Nested helpers, profile scripts, dynamically authored functions and script-loader calls use this closure through the isolated environment's `load`/`loadstring`/`dofile`. The client's global compiler functions are not replaced. Client-specific restrictions inside other native APIs are not assumed absent.

The complete OTCv8 game_bot runtime is pinned to upstream `aacfe3f1fe4bcadb5a34ff1f36263e1c96b3dd32` in `vendor/game_bot`. It is adapted at staging, not reimplemented feature by feature. The host, executor, panels, callbacks, configuration manager and original vBot profile run in one actual game LuaJIT. Helper closures therefore see the game's real globals and native objects. Local profile code remains the fork's original source. Other upstream example profiles are omitted; the fork profile is staged directly.

The private scope inherits the actual game `_G`. Its `modules.game_bot` facade is scoped only to this runtime; the client's real `modules` and `g_modules` are unchanged. No manifest is installed or registered. The module registry is compared before and after startup when `getModules` is available. There is no OS DLL hiding, module-list interception or obfuscation. UI styles are still normal client-wide OTUI definitions; unloading destroys owned widgets, but style definitions can remain in the client style registry.

## Adaptations

- Original source/resource loads resolve to staged paths under the existing client write directory. Dynamic Lua compilation goes through the DLL-provided compiler, not a global Lua wrapper.
- OTUI callbacks explicitly reference the private namespace. Bot window/button IDs and the game_bot settings node are separate from the client's existing bot instance.
- The profile context inherits the private runtime environment and actual client globals, including `_G` and custom native APIs. Profile variable writes stay private. Dynamic script loads retain that environment.
- The loader tracks owned widgets, event handlers and timers, and the original clear routine only destroys this runtime's owned widgets marked `botWidget`. Host/editor windows and other bots' widgets remain intact. Ownership uses weak keys so destroyed transient widgets are not retained forever. Its cleanup does not disable a shared tile callback setting.
- Existing core game APIs and `game_interface`, `client_topmenu`, `client_profiles`, text editors, JSON, UI styles and resource operations must exist in the target fork. Missing facilities are reported during actual full startup; they are not mocked or treated as supported.
- Startup imports all helpers/panels and the complete original profile before allowing the first macro tick. Compilation, initialization errors and the module-registry check are recorded. `startup-trace.json` is written under the staged profile location on unload/failure.
- Pause/resume uses game_bot's own enable/refresh path, so configuration/editor behavior and individual macro controls remain native. Closing the launcher unloads. Its five-second heartbeat watcher also unloads after a lost host. DLL code stays pinned until the client exits.

## Verification

The package test checks all runtime components, excludes `.otmod` files, and preserves existing saved profile configuration. A lifecycle fixture verifies actual `_G` lookup, private variables, unchanged existing `modules.game_bot` and module registry, callback return values, pause/resume and rollback after a startup failure. Separate regressions execute the original adapted executor and bot initialization/clear routines against small fixtures, checking private profile writes, client API calls and preservation of host/foreign windows. These fixtures are **not** proof that the full vBot UI has initialized.

A real C++ harness loads LuaJIT's exported native API, calls the exact new compiler closure with global `load`/`loadstring` blocked in that isolated test VM, exercises real game-table mutation and stack restoration, and checks syntax/type/size errors. It also compiles all 99 bundled runtime/profile Lua files. CI builds a standalone test LuaJIT pinned to official revision `b83bcba6ea300d4eb16e8257ddf80bcdef1f938b` on both systems, since Windows Lupa wheels do not export the required C API. This is only a test dependency; production uses the selected client's existing VM. The inherited build workflow compiles the actual controller and DLL on Windows.

The workspace cannot create an X11 display, so a real full-UI launch could not be demonstrated here. Actual Exoria injection, complete profile startup/UI behavior, runtime API compatibility, minimized dispatch and frame-time acceptance remain live gates. Do not describe this branch as universally compatible or claim all vBot features passed from the offline fixtures. Capture the generated startup trace and actual in-game behavior when validating the target client.
