"""Open the upstream CaveBot panel skeleton. Run with python external/window.py."""
from pathlib import Path
import tkinter as tk
from tkinter import ttk


PANEL = Path(__file__).resolve().parents[1] / "cavebot" / "cavebot.otui"


def panel_controls(source):
    """Read the list and switch declarations of CaveBotPanel in source order."""
    active = False
    controls = []
    current = None
    state = None
    for line in source.splitlines():
        value = line.strip()
        if not value:
            continue
        if not line.startswith(" "):
            active = value == "CaveBotPanel < Panel"
        if not active:
            continue
        if value in {"TextList", "BotSwitch"}:
            current = {"kind": value}
            controls.append(current)
            state = None
        elif ":" not in value:
            current = None
        elif current is not None:
            key, _, text = value.partition(":")
            if key == "id":
                current["id"] = text.strip()
            elif key in {"$on", "$!on"}:
                state = key
            elif key == "text" and state:
                current[state] = text.strip()
    return controls


def main():
    root = tk.Tk()
    root.title("vBotPac · CaveBot")
    panel = ttk.Frame(root, padding=8)
    panel.pack(fill="both", expand=True)
    for control in panel_controls(PANEL.read_text(encoding="utf-8")):
        if control["kind"] == "TextList":
            tk.Listbox(panel, name=control["id"].lower(), height=7).pack(
                fill="both", expand=True
            )
        else:
            # Editor and config modules have not been loaded in this host yet.
            ttk.Button(panel, text=control["$!on"], state="disabled").pack(fill="x")
    ttk.Label(panel, text="Offline · No broker session").pack(anchor="w")
    root.mainloop()


if __name__ == "__main__":
    main()
