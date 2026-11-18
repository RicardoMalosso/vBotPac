"""Open the upstream CaveBot panel skeleton. Run with python external/window.py."""
from pathlib import Path
from queue import Empty, Queue
from shutil import which
from subprocess import PIPE, STDOUT, Popen
from threading import Thread
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
            rows = tk.Listbox(panel, name=control["id"].lower(), height=7,
                              exportselection=False)
            rows.pack(fill="both", expand=True)
        else:
            # Editor and config modules have not been loaded in this host yet.
            ttk.Button(panel, text=control["$!on"], state="disabled").pack(fill="x")
    status = tk.StringVar(value="Opening offline replay…")
    step = ttk.Button(panel, text="Step replay", state="disabled")
    step.pack(fill="x")
    ttk.Label(panel, textvariable=status, wraplength=400).pack(anchor="w")
    runtime = next((path for name in ("luajit", "lua", "texlua")
                    if (path := which(name))), None)
    if runtime is None:
        status.set("Install LuaJIT and add luajit to PATH to open the replay.")
        root.mainloop()
        return
    try:
        worker = Popen([runtime, "external/replay.lua", "--interactive"],
                       cwd=PANEL.parents[1], stdin=PIPE, stdout=PIPE, stderr=STDOUT,
                       text=True, encoding="utf-8", errors="replace")
    except OSError as error:
        status.set(str(error))
        root.mainloop()
        return

    messages = Queue()
    ready = False
    def read_worker():
        with worker.stdout as stream:
            for line in stream:
                messages.put(line.rstrip("\r\n"))
        messages.put(None)

    def send(command):
        nonlocal ready
        if ready:
            ready = False
            step.state(["disabled"])
            try:
                worker.stdin.write(command + "\n")
                worker.stdin.flush()
            except OSError as error:
                status.set(str(error))

    def select(event):
        if ready and rows.size():
            index = rows.nearest(event.y)
            send(f"select {index + 1}")
        return "break"

    def poll():
        nonlocal ready
        while True:
            try:
                line = messages.get_nowait()
            except Empty:
                break
            if line is None:
                ready = False
                step.state(["disabled"])
                status.set("Replay stopped. " + last_error[0])
                return
            fields = line.split("\t")
            if fields[0] == "row" and len(fields) == 3:
                rows.insert("end", f"{fields[1]}: {fields[2]}")
            elif fields[0] == "focus" and len(fields) == 2:
                index = int(fields[1]) - 1
                rows.selection_clear(0, "end")
                rows.selection_set(index)
                rows.activate(index)
                rows.see(index)
                ready = True
                step.state(["!disabled"])
                status.set(f"Offline replay · Waypoint {index + 1} of {rows.size()}")
            else:
                last_error[0] = line
        root.after(20, poll)

    def close():
        worker.stdin.close()
        if worker.poll() is None:
            worker.terminate()
        worker.wait()
        root.destroy()

    last_error = [""]
    rows.bind("<Button-1>", select)
    rows.bind("<Key>", lambda event: "break")
    step.configure(command=lambda: send("step"))
    root.protocol("WM_DELETE_WINDOW", close)
    Thread(target=read_worker, daemon=True).start()
    poll()
    root.mainloop()


if __name__ == "__main__":
    main()
