# Run vBot in an external application

## Design decision

vBotPac supplies the original Lua scripts, loader, UI definitions, configuration and behavior. Build an OTCv8-compatible host in the new external application. Load vBot into that application's LuaJIT VM at runtime. Preserve upstream script files and load order wherever possible. CaveBot is the first compatibility test for the host. The same host must ultimately run the rest of vBot.

## Repositories and responsibilities

- **vBotPac** owns the external window, LuaJIT worker, compatible bot-script environment, UI rendering and local state cache. Keep new host code in a separate directory so upstream updates can be compared directly.
- **RicardoMalosso/PInj** contains the OtClientBot implementation. Its [progress log](https://github.com/RicardoMalosso/PInj/blob/main/docs/progress.md) describes the Python host, managed sessions, sequenced RPCs, session invalidation and broker action arbitration. Use the existing host and its contracts as the integration boundary. Confirm individual observations and actions against the checked-out revision. A catalog entry alone does not establish support for an operation.

## Implementation milestones

1. **Pin and inventory.** Record revisions of vBotPac, its compatible OTCv8 game_bot environment and PInj. Trace vBot startup from loader through module registration. Inventory the Lua helpers, globals, callbacks, OTUI widget classes, assets, storage and configuration used at startup and during play. For each dependency identify the upstream helper that can run locally, external host implementation, or supported broker operation. Record unsupported dependencies by module.
2. **Boot the external host offline.** Start a LuaJIT worker and an external window. Implement script-relative resources, module loading, persistent profile storage, a monotonic clock, timers and event dispatch. Run compatible upstream game_bot helpers directly. Use a recorded session for deterministic input. A script error should identify the module and missing API. Keep a baseline diff for upstream Lua and UI assets.
3. **Preserve UI semantics.** Evaluate reuse of OTCv8's widget and OTUI loader in the external host using the original CaveBot panel as a feasibility test. If an adapter is needed, implement only the widget operations exercised and expand with coverage. Preserve widget identity, list order, focus changes and callbacks. CaveBot reads the focused child as its current action, so changing focus must remain observable to the unchanged runner.
4. **Connect the existing broker.** Bind the external host to one managed session. Maintain external snapshots and stable session-scoped object proxies for reads such as player position and visible creatures. Translate supported action calls to broker requests. Schedule callbacks on the external Lua worker. Match each function's upstream return semantics, keeping submission, observation and failure distinct. On disconnect or lost ordering, pause affected modules, invalidate old proxies and refresh state before resuming. Implement missing capabilities once at the host or broker boundary.
5. **Prove CaveBot compatibility.** Load the original action registry, macro loop and walking code. Exercise retry, delay, position confirmation, map-click and directional walking, widget focus and last-to-first looping. Compare action and callback traces with upstream on matching fixtures. Reuse external pathfinding where its results satisfy the upstream helper contract. Track requested behavioral differences such as first-reachable startup separately from compatibility defects.
6. **Support runtime load and reload.** Give each loaded instance ownership of timers, widgets, callbacks and outstanding requests. Unload on the worker, discard stale replies and resynchronize after an already-issued action. Reload the same profile twice while the client stays open and verify a single macro and a single position listener remain.
7. **Grow coverage to the full profile.** Enable TargetBot, looting, healing and extensions against the same host contracts. Test arbitration and callback ordering with concurrent modules. Record any unavoidable upstream edit with the missing contract, rationale and regression test. Maintain a compatibility matrix by module and client profile.

## Release checks

- Launch the external window, select a session and load an existing vBot profile. Demonstrate that supplied CaveBot scripts run unchanged and existing config round-trips.
- Run planning, Lua timers, UI work and IPC waits outside the game thread. Bound client-thread observations and actions per tick. Test a stalled external worker, a full queue, a reconnect and a large map refresh. Compare frame-time percentiles and worst client-thread work against an idle-broker baseline on the same route.
- Compare upstream and external traces for the same position changes and action results. Report unsupported calls explicitly. Publish which modules passed offline replay and which passed live testing.
