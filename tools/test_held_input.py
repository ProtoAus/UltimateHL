"""Bounded offline W+A test. Temporarily injects keys ONLY into its own focused game.

Requires --allow-input. Stops if W/A are already held. Always attempts to release
injected keys, closes only its own child and restores shared video preferences.
Does not force foreground or attach another application's input queue.
The specified cfg must start a trace, load a disposable save, stop, and quit.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import subprocess
import time
import winreg

PROJECT = Path(__file__).resolve().parents[1]
u = c.WinDLL("user32", use_last_error=True)
u.GetForegroundWindow.restype = w.HWND
u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
u.GetAsyncKeyState.argtypes = [c.c_int]
u.GetAsyncKeyState.restype = c.c_short
u.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]


class Mouse(c.Structure):
    _fields_ = [("dx", w.LONG), ("dy", w.LONG), ("data", w.DWORD), ("flags", w.DWORD),
                ("time", w.DWORD), ("extra", c.c_size_t)]


class Keyboard(c.Structure):
    _fields_ = [("vk", w.WORD), ("scan", w.WORD), ("flags", w.DWORD),
                ("time", w.DWORD), ("extra", c.c_size_t)]


class Payload(c.Union):
    _fields_ = [("mouse", Mouse), ("keyboard", Keyboard)]


class Input(c.Structure):
    _fields_ = [("type", w.DWORD), ("payload", Payload)]


u.SendInput.argtypes = [w.UINT, c.POINTER(Input), c.c_int]
u.SendInput.restype = w.UINT


def send(vk, up):
    event = Input(type=1, payload=Payload(keyboard=Keyboard(vk=vk, flags=2 if up else 0)))
    if u.SendInput(1, c.byref(event), c.sizeof(event)) != 1:
        raise OSError(c.get_last_error(), "SendInput failed")


def focused_pid():
    pid = w.DWORD()
    hwnd = u.GetForegroundWindow()
    u.GetWindowThreadProcessId(hwnd, c.byref(pid))
    return hwnd, pid.value


def video_snapshot():
    path = r"Software\Valve\Half-Life\Settings"
    names = ("ScreenWidth", "ScreenHeight", "ScreenBPP", "ScreenWindowed", "EngineDLL")
    values = {}
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
            for name in names:
                try: values[name] = winreg.QueryValueEx(key, name)
                except FileNotFoundError: values[name] = None
    except FileNotFoundError:
        values = dict.fromkeys(names)
    return path, values


def restore_video(snapshot):
    path, values = snapshot
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, path) as key:
        for name, value in values.items():
            if value is None:
                try: winreg.DeleteValue(key, name)
                except FileNotFoundError: pass
            else:
                winreg.SetValueEx(key, name, 0, value[1], value[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("installation", type=Path)
    parser.add_argument("--script", default="uhlholdbase.cfg")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-input", action="store_true")
    parser.add_argument("--press-during-load", action="store_true", help="Wait for the flushed load_begin marker before pressing W+A")
    args = parser.parse_args()
    if not args.allow_input:
        raise ValueError("This test uses real injected W/A state. Explicit --allow-input is required.")
    if args.output.exists(): raise ValueError("Use a NEW result filename")
    if not (args.installation / "stage-manifest.json").is_file():
        raise ValueError("Only a separately staged playtest installation is permitted")
    metadata = json.loads((args.installation / "stage-manifest.json").read_text())
    if metadata.get("adapter_version") != 3 or "blocked" not in metadata.get("windows_mouse_settings_policy", ""):
        raise ValueError("Only a v3 installation with the Windows mouse-settings guard is permitted")
    if Path(args.script).name != args.script or not args.script.endswith(".cfg"):
        raise ValueError("Provide a simple bounded cfg filename, not a path")
    game = (args.installation / "Half-Life").resolve()
    if not (game / "valve_WON" / args.script).is_file(): raise ValueError("Missing bounded test cfg")
    if any(u.GetAsyncKeyState(key) & 0x8000 for key in (ord("W"), ord("A"))):
        raise ValueError("W or A is already held; refusing to interfere with existing input")
    old = set(game.glob("uhl-input-*.tsv"))
    snapshot = video_snapshot()
    injected = []
    process = None
    trace = None
    held_samples = []
    try:
        environment = os.environ.copy()
        environment["UHL_BORDERLESS"] = "0"  # A small diagnostic window must not be stretched to desktop size.
        process = subprocess.Popen([str(game / "hl.exe"), "-steam", "-game", "valve_WON", "-insecure",
            "-nomaster", "-noforcemparms", "-noforcemaccel", "-noforcemspd", "-windowed", "-w", "800", "-h", "600", "-gl", "-nosound", "-condebug",
            "+wait", "+exec", args.script], cwd=game, env=environment)
        started = time.monotonic()
        while process.poll() is None and time.monotonic() - started < 60:
            traces = [p for p in game.glob("uhl-input-*.tsv") if p not in old]
            if traces and not trace: trace = max(traces, key=lambda p: p.stat().st_mtime)
            ready = trace and (not args.press_during_load or "\tload_begin\t" in trace.read_text())
            if ready and not injected:
                print("Trace:", trace.name, "process PID:", process.pid, flush=True)
                hwnd, pid = focused_pid()
                if pid != process.pid:
                    raise RuntimeError("Diagnostic game isn't foreground; no keys injected")
                for vk in (ord("W"), ord("A")):
                    if focused_pid()[1] != process.pid: raise RuntimeError("Focus changed; aborting key injection")
                    send(vk, False)
                    injected.append(vk)
                print("Holding W+A in the owned foreground game; cleanup will release both.", flush=True)
            if injected:
                held_samples.append([bool(u.GetAsyncKeyState(key) & 0x8000) for key in injected])
                if focused_pid()[1] != process.pid and process.poll() is None:
                    # Quitting can briefly clear foreground while the process exits.
                    time.sleep(.1)
                    if process.poll() is None and focused_pid()[1] != process.pid:
                        raise RuntimeError("Game lost focus during input test")
            time.sleep(.01)
        if process.poll() is None: raise TimeoutError("Diagnostic child exceeded 60 seconds")
        if not trace: raise RuntimeError("No input trace: wrong game/client or trace commands unavailable")
        rows = []
        for line in trace.read_text().splitlines()[1:]:
            fields = line.split("\t")
            if len(fields) == 6: rows.append(fields)
        begin = next(i for i, row in enumerate(rows) if row[2] == "load_begin")
        end = next(i for i, row in enumerate(rows[begin:], begin) if row[2] == "load_return")
        before = [int(row[3]) for row in rows[:begin] if row[2] == "usercmd" and row[5] == "1"]
        after = [int(row[3]) for row in rows[end + 1:] if row[2] == "usercmd" and row[5] == "1"]
        result = {"trace": str(trace), "process_exit": process.returncode,
            "press_timing": "during-load" if args.press_during_load else "before-load",
            "held_keys_physically_down_during_samples": all(all(row) for row in held_samples[:-2]),
            "preload_last_buttons": before[-1] if before else None,
            "postload_first_buttons": after[:20], "postload_unique_buttons": sorted(set(after)),
            "forward_and_left_preserved": bool(after) and all(buttons & 0x208 == 0x208 for buttons in after),
            "transition_events": [row for row in rows if row[2] != "usercmd"]}
        with args.output.open("x") as file: json.dump(result, file, indent=2)
        print(json.dumps({key: value for key, value in result.items() if key != "transition_events"}, indent=2))
    finally:
        for key in reversed(injected):
            try: send(key, True)
            except OSError as error: print(f"Key release failed for {key}: {error}", flush=True)
        try:
            if process and process.poll() is None:
                # Only the test process, never another hl.exe instance.
                process.terminate()
                process.wait(timeout=5)
        finally:
            restore_video(snapshot)


if __name__ == "__main__":
    main()
