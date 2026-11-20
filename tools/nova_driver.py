#!/usr/bin/env python3
"""Control the EV Nova test build directly, without desktop automation."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

if __package__:
    from .scenario_runner import Probe
else:
    from scenario_runner import Probe

ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = ROOT / "analysis" / "driver"


class NovaDriver(Probe):
    """Input-only control; state snapshots never modify the simulation."""

    def __init__(self, port: int = 8190):
        super().__init__(f"http://127.0.0.1:{port}")

    def state(self, query: str = "summary") -> object:
        return self.get("/probe/state?query=" + urllib.parse.quote(query))

    def ui(self) -> object:
        return self.get("/probe/ui")

    def screenshot(self) -> bytes:
        # The first GPU readback after a fullscreen transition can outlast
        # the server's frame wait. Retry only its explicit not-ready response.
        for attempt in range(3):
            try:
                return super().screenshot()
            except urllib.error.HTTPError as error:
                if error.code != 503 or attempt == 2:
                    raise
                error.close()
                time.sleep(0.1)
        raise RuntimeError("No screenshot captured")

    def command(self, cmd: str, **fields: object) -> object:
        return self.post("/probe/command", {**fields, "cmd": cmd})

    def click(self, element: str) -> object:
        return self.post("/probe/click", {"element": element})

    def key(self, key: str) -> object:
        return self.post("/probe/key", {"key": key})

    def hold(self, keys: list[str], down: bool = True) -> object:
        return self.post("/probe/hold", {"keys": keys, "down": down})

    def mouse(self, x: int, y: int, *, button: str | None = None,
              down: bool | None = None, wheel: int = 0) -> object:
        fields: dict[str, object] = {"x": round(x), "y": round(y), "wheel": wheel}
        if button is not None:
            if button not in ("left", "right") or down is None:
                raise ValueError("Mouse buttons need left/right and a down state")
            fields.update(button=button, down=down)
        return self.post("/probe/mouse", fields)

    def type(self, text: str) -> None:
        # These dialogs consume physical key events, not clipboard paste.
        # Validate the whole string before delivering any keys.
        if any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 " for c in text):
            raise ValueError("Driver text currently supports letters, digits and spaces.")
        for char in text:
            self.key("Space" if char == " " else char.lower())


def start_game(exe: Path, port: int, headed: bool, profile: Path,
               pilot: Path | None = None) -> dict[str, object]:
    exe = exe.resolve()
    if not exe.is_file():
        raise ValueError(f"Build the driver first: tools/build-windows.ps1 -TestDriver (missing {exe})")
    if pilot is not None and not pilot.is_file():
        raise ValueError(f"Pilot file not found: {pilot}")
    # Fail before launching if this port belongs to an existing session.
    with socket.socket() as check:
        check.bind(("127.0.0.1", port))
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    profile.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(EVN_PROBE="1", EVN_PROBE_PORT=str(port),
               EVN_PROBE_PROFILE=str(profile.resolve()),
               EVNOVA_LOG_FILE=str(ARTIFACTS / f"game-{port}.log"))
    env.pop("EVN_PROBE_PILOT_FILE", None)
    if pilot is not None:
        env["EVN_PROBE_PILOT_FILE"] = str(pilot.resolve())
    if not headed:
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   SDL_RENDER_DRIVER="software")
    else:
        for key in ("SDL_VIDEODRIVER", "SDL_AUDIODRIVER", "SDL_RENDER_DRIVER"):
            env.pop(key, None)
    with (ARTIFACTS / f"process-{port}.log").open("wb") as log:
        process = subprocess.Popen(
            [str(exe)], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
            stdout=log, stderr=log,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
    driver = NovaDriver(port)
    deadline = time.monotonic() + 45
    try:
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError(f"Game exited with {process.returncode}; see {ARTIFACTS}")
            try:
                # Wait for the actual game menu, not just a bound socket.
                if driver.ui().get("window") == "main_menu":
                    session = {"pid": process.pid, "port": port, "exe": str(exe),
                               "headed": headed, "base_url": driver.base_url}
                    (ARTIFACTS / f"session-{port}.json").write_text(
                        json.dumps(session, indent=2), encoding="utf-8")
                    return session
            except (OSError, ValueError):
                pass
            time.sleep(0.1)
        raise TimeoutError(f"Game did not reach its menu; see {ARTIFACTS}")
    except BaseException:
        # Only terminate the child this invocation created.
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8190)
    commands = parser.add_subparsers(dest="action", required=True)
    start = commands.add_parser("start", help="Start a headless game and wait for its menu")
    start.add_argument("--exe", type=Path, default=ROOT / "build/driver-release/src/evnova.exe")
    start.add_argument("--pilot", type=Path, help="File returned when Open Pilot is clicked (test build only)")
    start.add_argument("--headed", action="store_true", help="Show the game window")
    start.add_argument("--profile", type=Path, default=ARTIFACTS / "profile",
                       help="Separate test pilot and preferences directory")
    commands.add_parser("ui", help="Read named controls in the active screen")
    state = commands.add_parser("state", help="Read game state")
    state.add_argument("query", nargs="?", default="summary")
    click = commands.add_parser("click", help="Click a named game control")
    click.add_argument("element")
    key = commands.add_parser("key", help="Tap a game key")
    key.add_argument("key")
    text = commands.add_parser("type", help="Type simple text through game key events")
    text.add_argument("text")
    mouse = commands.add_parser("mouse", help="Move, hold a mouse button, or zoom")
    mouse.add_argument("x", type=int)
    mouse.add_argument("y", type=int)
    mouse.add_argument("--button", choices=("left", "right"))
    mouse.add_argument("--wheel", type=int, default=0)
    mouse_state = mouse.add_mutually_exclusive_group()
    mouse_state.add_argument("--down", dest="down", action="store_const", const=True)
    mouse_state.add_argument("--up", dest="down", action="store_const", const=False)
    hold = commands.add_parser("hold", help="Hold/release flight controls")
    hold.add_argument("keys", nargs="+")
    hold.add_argument("--release", action="store_true")
    command = commands.add_parser("command", help="Pause, step, fly, land, trade, or quit")
    command.add_argument("cmd")
    command.add_argument("--args", default="{}", help="Additional command fields as JSON")
    screenshot = commands.add_parser("screenshot", help="Capture the game renderer")
    screenshot.add_argument("path", type=Path)
    logs = commands.add_parser("logs")
    logs.add_argument("--since", type=int, default=0)
    commands.add_parser("stop", help="Ask the game to quit normally")
    scenario = commands.add_parser("scenario", help="Run a repeatable TOML scenario")
    scenario.add_argument("path", type=Path)
    scenario.add_argument("--artifacts", type=Path, default=ARTIFACTS / "scenarios")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    driver = NovaDriver(args.port)
    try:
        if args.action == "start":
            result = start_game(args.exe, args.port, args.headed, args.profile, args.pilot)
        elif args.action == "ui":
            result = driver.ui()
        elif args.action == "state":
            result = driver.state(args.query)
        elif args.action == "click":
            result = driver.click(args.element)
        elif args.action == "key":
            result = driver.key(args.key)
        elif args.action == "type":
            driver.type(args.text)
            result = {"ok": True}
        elif args.action == "hold":
            result = driver.hold(args.keys, not args.release)
        elif args.action == "mouse":
            result = driver.mouse(args.x, args.y, button=args.button,
                                  down=args.down, wheel=args.wheel)
        elif args.action == "command":
            fields = json.loads(args.args)
            if not isinstance(fields, dict) or "cmd" in fields:
                raise ValueError("--args must be a JSON object without a cmd field")
            result = driver.command(args.cmd, **fields)
        elif args.action == "screenshot":
            args.path.parent.mkdir(parents=True, exist_ok=True)
            args.path.write_bytes(driver.screenshot())
            result = {"path": str(args.path.resolve())}
        elif args.action == "logs":
            result = driver.get(f"/probe/logs?since={args.since}")
        elif args.action == "stop":
            result = driver.command("quit")
        elif args.action == "scenario":
            return subprocess.call([
                sys.executable, str(ROOT / "tools/scenario_runner.py"), str(args.path),
                "--base-url", driver.base_url, "--artifacts", str(args.artifacts),
            ], cwd=ROOT)
        else:
            raise ValueError(f"Unknown action: {args.action}")
        print(json.dumps(result, indent=2, ensure_ascii=True))
        return 0
    except urllib.error.HTTPError as error:
        print(f"Driver error {error.code}: {error.read().decode('utf-8', 'replace')}", file=sys.stderr)
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Driver error: {error}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
