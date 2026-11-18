"""Read a single authenticated OtClientBot host session over loopback."""
from http.client import HTTPConnection
import json
from urllib.parse import parse_qs, urlsplit


class Broker:
    def __init__(self, launch_url, pid):
        url = urlsplit(launch_url.strip())
        tokens = parse_qs(url.fragment).get("token", [])
        if (url.scheme != "http" or url.hostname != "127.0.0.1" or not url.port
                or url.username or url.password or len(tokens) != 1):
            raise ValueError("Expected the broker launch URL with its #token fragment")
        self.port = url.port
        self.token = tokens[0]
        self.pid = pid
        self.identity = None

    def read(self):
        conn = HTTPConnection("127.0.0.1", self.port, timeout=3)
        try:
            conn.request("GET", "/api/state", headers={"X-Session-Token": self.token})
            response = conn.getresponse()
            if response.status != 200:
                raise ValueError(f"Broker returned HTTP {response.status}")
            state = json.load(response)
        finally:
            conn.close()
        if state.get("mode") not in ("live-managed", "live-readonly") or state.get("stalled"):
            raise ValueError("A responsive live broker session is required")
        pid = state["selected_client"]["pid"]
        if self.pid is not None and self.pid != pid:
            raise ValueError(f"Broker selected PID {pid}, expected {self.pid}")
        identity = (pid, state["runtime_id"], state["session"])
        if self.identity is not None and self.identity != identity:
            raise ValueError("The selected broker session changed")
        world = state["world"]
        player = world.get("player")
        if not world.get("online") or not isinstance(player, dict):
            raise ValueError("The selected client has no online player")
        position = player.get("position")
        if not isinstance(position, dict) or not all(type(position.get(k)) is int for k in ("x","y","z")):
            raise ValueError("Broker has no current player position")
        self.identity = identity
        return {"pid": pid, "name": player.get("name") or "Player", "position": position}
