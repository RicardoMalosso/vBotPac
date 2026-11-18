# External vBot window and runtime

## Outcome

Open a separate bot window, select a broker session, load a CaveBot configuration and start or stop its route while OTClient keeps running. Load Lua modules into a dedicated external LuaJIT worker at runtime. The worker owns bot decisions, pathfinding and timers. The existing OtClientBot broker is the integration point for session state and supported actions.

## Starting point

Reviewed vBotPac main at 09d6816 (v4.8). In [cavebot.lua](../cavebot/cavebot.lua), the 20 ms macro handles retries and coordinates walking and TargetBot. Widget focus doubles as the current waypoint. In [walking.lua](../cavebot/walking.lua), movement bookkeeping depends on position-change callbacks, direction order, timing and the configured walking mode. The goto handler in [actions.lua](../cavebot/actions.lua) also interacts with combat in its stuck-recovery branch. Preserve these behaviors explicitly as each dependency becomes available.

The broker source was unavailable during planning. The contracts below are proposed requirements. Verify which are already implemented before adding anything.

## Implementation order

1. **Inventory the integration.** Pin both revisions. Trace the broker's public session, snapshot, event, map and action interfaces and identify which thread executes each operation. For the starter CaveBot config, list every required global, callback, storage key, widget dependency and action. Map each to an existing broker capability, external implementation or explicit unsupported feature. Record a short timestamped session trace for offline replay.
2. **Ship the first vertical slice.** Launch a separate window and LuaJIT worker, select a broker session, and display the character position and connection status. Feed Lua an immutable local snapshot. Deliver ordered movement events with session identity and sequence numbers. On a gap or reconnect, pause the runner and refresh the snapshot before resuming. Verify the same position sequence using live and replay inputs.
3. **Implement the module host.** Supply the clock, macro scheduler, position callbacks, configuration and storage needed by the first module. Load CaveBot on demand. Track timers, callbacks and pending intents per module instance. Stop or reload by retiring that instance, cancelling outstanding work where possible and ignoring late responses. Resynchronize position after an already-issued step. Reload twice and verify one active macro and one position listener.
4. **Port the route runner.** Extract ordered actions and the active index from the widget list into shared route state. Retain upstream action dispatch, retry counts, delays, previous-action results and last-to-first looping. Preserve the route configuration format and round-trip unported actions without losing their settings. Show an actionable unsupported-action error on execution. Replay an existing route and compare its action sequence with vBot.
5. **Connect walking through the broker.** Run path search against external map data. Adapt the movement calls in walking.lua to the broker's supported actions and feed observed position changes back into its walking state. Distinguish command acceptance from actual movement. Keep position events ordered even if the UI coalesces redraws. Test walking around a wall, starting midway at the first reachable waypoint, an intentional floor transition, a blocked step and a route wrapping to its start. Keep unreachable waypoints and retry reasons visible.
6. **Finish the external window.** Provide session selection, module load/reload, start/stop, current waypoint, route editing, map and recent action outcomes. Keep the CaveBot map and route controls together. A separate Route Planner can use its own map view. Route state belongs to the worker so changing tabs or selecting an editor row cannot advance the running route.
7. **Expand by feature.** After CaveBot passes replay and live checks, port TargetBot and looting with their movement arbitration, then add extensions one at a time. Each module declares required capabilities and demonstrates its behavior in replay before it becomes selectable for live use.

## Game-thread acceptance criteria

- Run IPC waits, path searches, bulk map serialization and Lua module execution off the game thread. If the client API requires a main-thread read or action, execute bounded work through the broker's supported client integration.
- Bound queues and expire stale movement requests. Pause and resynchronize on overflow or loss of ordered events. Never replay pending movement into a different session.
- Measure baseline and active p95/p99 frame times on the same client and route. Record maximum per-tick broker work, queue depth and event age. Include a stalled worker, disconnected window and bulk map refresh. Confirm the game continues advancing during each case and agree a frame-time budget from the baseline before release.

The first playable milestone is complete when an existing route runs from the external window, loops, survives a module reload without duplicate actions, and produces a trace linking each waypoint advance to observed game state.
