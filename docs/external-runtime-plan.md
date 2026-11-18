# Run vBot in an external application

## Design decision

vBotPac supplies the original Lua scripts, loader, UI definitions, configuration and behavior. Build an OTCv8-compatible host in the new external application. Load vBot into that application's LuaJIT VM at runtime. Preserve upstream script files and load order wherever possible. The first deliverable is a complete external startup of the unmodified vBot profile. Defer individual feature tests until this has passed.

## Repositories and responsibilities

- **vBotPac** owns the external window, LuaJIT worker, compatible bot-script environment, UI rendering and local state cache. Keep new host code in a separate directory so upstream updates can be compared directly.
- **RicardoMalosso/PInj** contains the OtClientBot implementation. Its [progress log](https://github.com/RicardoMalosso/PInj/blob/main/docs/progress.md) describes the Python host, managed sessions, sequenced RPCs, session invalidation and broker action arbitration. Use the existing host and its contracts as the integration boundary. Confirm individual observations and actions against the checked-out revision. A catalog entry alone does not establish support for an operation.

## Implementation milestones

1. **Install the application runtime.** Bundle LuaJIT or install a pinned build inside the application directory during setup. Launch it by its application-relative path. Verify on a clean Windows installation with no Lua on PATH.
2. **Reproduce OTCv8 startup.** Use [the OTCv8 executor](https://github.com/OTCv8/otclientv8/blob/master/modules/game_bot/executor.lua) as the reference for resource discovery, the shared bot context, helper and panel scripts, OTUI style imports, and root script execution. Use the original profile loader as the source of module order. The reviewed loader lists 36 direct scripts and several of those load additional resources. Include nested loads and profile root scripts in readiness checks.
3. **Build the complete external window.** Supply the widget, layout, font, asset, event, config and storage services exercised during startup. Prefer reusing the upstream UI runtime when feasible. Wire startup-time game observations to session-scoped broker data. Handle the absence of a selected character explicitly. Keep the application event loop and Lua execution in the external process.
4. **Demonstrate a complete launch.** Instrument each script and style load with its path, parent and result. Record initialization errors including errors logged or caught by the runtime. Compare the loaded resources and registered tabs, panels, controls, macros and callbacks against the same profile and configuration in OTCv8. Verify the event loop paints the full external window, can switch between all initialized tabs and remains responsive. Capture the actual window and preserve the trace. Treat skipped modules, dummy widgets and missing dependencies as incomplete. A replay fixture cannot satisfy this milestone.
5. **Then verify individual features.** Exercise the original actions, retries, movement observations and inter-module arbitration through the existing broker. Keep upstream changes to a documented minimum.

## Release checks

- Launch the external window, select a session and load an existing vBot profile. Demonstrate that supplied CaveBot scripts run unchanged and existing config round-trips.
- Run planning, Lua timers, UI work and IPC waits outside the game thread. Bound client-thread observations and actions per tick. Test a stalled external worker, a full queue, a reconnect and a large map refresh. Compare frame-time percentiles and worst client-thread work against an idle-broker baseline on the same route.
- Compare upstream and external traces for the same position changes and action results. Report unsupported calls explicitly. Publish which modules passed offline replay and which passed live testing.
