# In-client vBot runtime

Branch: `feat/in-client-vbot-module`. The external implementation on `main` is unchanged.

## Execution and ownership

The controller selects a fingerprint-checked Windows x64 process and binds its owning Lua state/thread through the existing session gate. `module-v1` dispatch compiles the bootstrap on that thread. Capability version 2 supplies a private compiler and a private-file factory as two bootstrap arguments. Old loaded DLLs are rejected before loading code. Nested script loads use the private compiler without replacing global `load`/`loadstring`.

The complete OTCv8 `game_bot` runtime is pinned to `aacfe3f1fe4bcadb5a34ff1f36263e1c96b3dd32` in `vendor/game_bot`. The original vBot profile stays at the repository root. Other upstream example profiles and `.otmod` manifests are omitted. All helpers, panels, profile scripts and callbacks run in the actual game VM. The profile environment inherits real client globals, `_G` and native objects, while profile writes stay private.

Only the scoped `modules.game_bot` facade references this runtime. The actual `modules` table and `g_modules` registry remain unchanged. No resource search path or mount is added. The ordinary Windows DLL remains visible. Owned events, connections and widgets are cleaned up on unload; shared native style definitions can remain cached.

## Project-owned data and logs

The host stages resources under `<repository>/data/inclient/`. A DLL-provided file closure is bound to the canonical absolute data root. Reads, sorted directory listings, directory creation, atomic replacement, append and removal operate within that root. Relative traversal, Windows drive/stream paths, root removal and symlink escapes are rejected. Native operations run only on the bound game Lua thread/state and return errors to Lua rather than calling the client logger. Writes preserve binary data and Windows Unicode paths.

Virtual resource paths retain `/vbotpac_inclient/...` naming, but files are no longer placed in the client write directory. Existing character/fingerprint profile data is copied read-only from the old location when missing locally. New private saves are never replaced by old data. The old client settings node is read once for migration; subsequent bot settings go to a private JSON file. Client settings reads needed for game/profile context remain available. The bot miniwindow does not request the client's persistent window settings.

The scoped `print` and `g_logger` methods append diagnostics to `data/inclient/vbotpac_inclient/logs/inclient.log`. Script errors, status messages and startup traces remain available there and in the launcher; they are not forwarded to the global client logger. No existing client logs are edited or filtered. Native client code may still log its own parser/widget/game errors. Sharing its native UI/game engine does not provide complete logging isolation.

## UI isolation and compatibility

Own OTUI files are read through the private file closure and passed to `importStyleFromString`/`loadUIFromString`. Native path-based UI loading is not used for bot resources. The bundled configuration image is embedded with `image-source-base64`, so it does not enter the client's encrypted image-file reader. Client-provided image paths retain their native behavior.

All 164 shipped style names, plus dynamic style declarations, receive a private `VBPM_` prefix. Their inheritance, widget construction and inline UI references are mapped consistently; original client styles are not replaced. Font names are checked through `g_fonts.fontExists`; unavailable fonts inherit the client default instead of invoking a missing native font. Child widgets are bound to their existing IDs after creation, covering fork differences in Lua child-field lookup. These adaptations address the uploaded `extras.lua`/`closeButton` startup failure without skipping Extras or substituting dummy controls. Actual target UI behavior still requires live confirmation.

The earlier native `ui/basic.otui` decrypt crash occurred because plaintext resources were passed to an encrypted client's file reader. Its `/bot/` raw-resource branch was confirmed in the fingerprint-matched binary (`bc6f786837345a73ddb80e8917640d0d97a2ae16835db5623edca979229065c2`, compare at RVA `0x22b9a0`, raw-read branch `0x22ba97`). The intermediate layout correction remains enforced for compatibility, while current project-owned in-memory loading removes that file-loader path for our UI entirely.

## Lifecycle and verification

Startup loads every helper/panel and the full original profile before enabling macro ticks. Errors and compiled-script names are recorded in the private startup trace. Pause/resume uses the original enable/refresh path. Closing the launcher unloads; a five-second lost-heartbeat watcher also unloads. DLL code stays pinned until the client exits.

Package and lifecycle fixtures verify migration, saved config preservation, actual global lookup, private profile writes, unchanged client module registry, callback returns, pause/resume, rollback, private logger/settings/resource writes, style names, font fallback and child binding. Client file loaders, settings writers and logger methods are configured to fail if accidentally called in the isolation fixture.

The C++ harness calls the actual compiler and private-file closures against real LuaJIT. It checks binary/Unicode file IO, atomic overwrite, separate roots, traversal/symlink guards, blocked global Lua compiler wrappers, stack restoration and syntax/type/size errors. All 99 bundled Lua files are syntax-compiled. CI builds pinned standalone test LuaJIT `b83bcba6ea300d4eb16e8257ddf80bcdef1f938b` on Windows/Linux and builds the actual Windows controller/DLL. Production uses the target client's existing VM.

These fixtures do not demonstrate full vBot UI startup. Actual Exoria injection, profile/API compatibility, UI behavior, minimized dispatch and frame-time acceptance remain live gates. The uploaded trace confirmed the previous native resource-layout fix reached all helpers and panels before the profile-window failure.
