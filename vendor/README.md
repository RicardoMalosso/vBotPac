# Upstream runtime provenance

`game_bot` contains the runtime Lua, OTUI and image resources from `OTCv8/otclientv8` commit `aacfe3f1fe4bcadb5a34ff1f36263e1c96b3dd32`, the same pinned runtime used by the external branch. Example configurations and the `.otmod` registration manifest are intentionally not included. Original project: https://github.com/OTCv8/otclientv8/tree/aacfe3f1fe4bcadb5a34ff1f36263e1c96b3dd32/modules/game_bot

Runtime adaptation is centralized in `inclient/package.py` and `inclient/bootstrap.lua`. The vBot profile stays at the repository root and retains its upstream history/attribution.
