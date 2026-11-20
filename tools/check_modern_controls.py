#!/usr/bin/env python3
"""Exercise the real flight controls through an already-running test driver."""
from __future__ import annotations

import json
import math
from pathlib import Path
import time

from nova_driver import ARTIFACTS, NovaDriver


def wait_for(read, predicate, timeout=12):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = read()
        if predicate(last):
            return last
        time.sleep(0.04)
    raise AssertionError(f"Timed out; last observation: {last}")


def check() -> None:
    d = NovaDriver()
    results = []
    ARTIFACTS.mkdir(parents=True, exist_ok=True)

    def passed(name, **details):
        results.append({"check": name, "passed": True, **details})
        print(f"PASS {name}", flush=True)

    def flight():
        return wait_for(d.ui, lambda u: u["window"] == "")

    def radial(x, y):
        d.mouse(x, y, button="right", down=True)
        return wait_for(d.ui, lambda u: u["window"] == "flight_radial")

    def release_on(element):
        rect = d.ui()["rects"][element]
        x, y = rect[0] + rect[2] / 2, rect[1] + rect[3] / 2
        d.mouse(x, y)
        d.mouse(x, y, button="right", down=False)

    def empty_radial():
        view = d.state("view")
        for x, y in [(30, 30), (30, view["window_height"] - 80),
                     (view["player_screen_x"] * 2 - 40, 35),
                     (view["player_screen_x"], 40)]:
            ui = radial(round(x), round(y))
            if "radial.map" in ui["rects"]:
                return ui
            release_on("cancel")
            flight()
        raise AssertionError("No empty-space radial menu found")

    def visible_point(row, state, view):
        x = view["player_screen_x"] + (row["pos_x"] - state["pos_x"]) * view["zoom"]
        y = view["player_screen_y"] + (row["pos_y"] - state["pos_y"]) * view["zoom"]
        return (round(x), round(y)) if 20 < x < view["player_screen_x"] * 2 - 20 and 20 < y < view["window_height"] - 40 else None

    try:
        if d.ui()["window"] == "flight_radial":
            d.click("cancel")
        flight()
        assert d.state()["game_active"]
        d.command("accelerate", enabled=False)
        view = d.state("view")
        x, y = round(view["player_screen_x"] + 230), round(view["player_screen_y"])
        d.mouse(x, y, button="left", down=True)
        turned = wait_for(d.state, lambda s: abs(s["heading"] - math.pi / 2) < 0.15 and s["vel_x"] > 0)
        passed("hold left mouse steers and thrusts toward pointer", heading=turned["heading"])
        moving = wait_for(d.state, lambda s: math.hypot(s['vel_x'], s['vel_y']) > 2)
        d.mouse(x, y, button="left", down=False)
        stopped = wait_for(d.state, lambda s: math.hypot(s['vel_x'], s['vel_y']) < .001)
        assert abs(stopped['heading'] - moving['heading']) < .1
        d.mouse(40, 40)
        time.sleep(0.3)
        assert math.hypot(d.state()['vel_x'], d.state()['vel_y']) < .001
        passed("release brakes to a complete stop without turning", before=moving, after=stopped)

        d.mouse(x, y, button="left", down=True)
        wait_for(d.state, lambda s: s['vel_x'] > 1)
        d.mouse(x, y, button="left", down=False)
        d.hold(['Up'])
        wait_for(d.state, lambda s: s['vel_x'] > 2)
        d.hold(['Up'], down=False)
        coasting = d.state()['vel_x']
        time.sleep(.3)
        assert d.state()['vel_x'] > coasting * .9
        passed("keyboard flight overrides automatic braking")
        d.mouse(x, y, button="left", down=True)
        time.sleep(.1)
        d.mouse(x, y, button="left", down=False)
        wait_for(d.state, lambda s: math.hypot(s['vel_x'], s['vel_y']) < .001)

        # Queue press and release together: the flight event drain must leave
        # the release for the radial, including menus shifted at screen edges.
        for point in [(30, 30), (30, view['window_height'] - 30), (400, 400)]:
            d.command('pause')
            d.mouse(*point, button='right', down=True)
            d.mouse(*point, button='right', down=False)
            d.command('resume')
            time.sleep(.15)
            assert d.ui()['window'] == ''
        passed("quick right taps cancel even at screen edges")

        # Zoom is persisted, so establish a known end of its range first.
        d.mouse(x, y, wheel=20)
        wait_for(lambda: d.state("view"), lambda v: v["zoom"] > 1.5)
        original_view = d.state("view")
        d.mouse(x, y, wheel=-4)
        zoomed = wait_for(lambda: d.state("view"), lambda v: v["zoom"] < original_view["zoom"] * 0.8)
        assert zoomed["world_width"] > original_view["world_width"]
        assert zoomed["world_height"] > original_view["world_height"]
        assert zoomed["ui_scale"] == original_view["ui_scale"]
        passed("wheel shows more world while preserving HUD scale", before=original_view, after=zoomed)
        d.mouse(x, y, wheel=-20)
        wait_for(lambda: d.state("view"), lambda v: v["zoom"] == 0.5)

        ui = empty_radial()
        for rect in ui["rects"].values():
            assert rect[0] >= 0 and rect[1] >= 0
            assert rect[0] + rect[2] <= ui["window_size"][0]
            assert rect[1] + rect[3] <= ui["window_size"][1]
        (ARTIFACTS / "radial-flight.bmp").write_bytes(d.screenshot())
        before = d.state()
        time.sleep(0.15)
        after = d.state()
        assert before["pos_x"] == after["pos_x"] and before["pos_y"] == after["pos_y"]
        passed("radial menu pauses flight and fits screen edges")
        release_on("cancel")
        flight()
        passed("right release in the center closes the radial")
        empty_radial()
        d.mouse(0, 0, button="right", down=False)
        flight()
        passed("right release outside the radial cancels")
        empty_radial()
        d.key("Escape")
        d.mouse(0, 0, button="right", down=False)
        flight()
        assert d.state()["game_active"]
        passed("Escape dismisses radial without leaving flight")

        empty_radial()
        release_on("radial.map")
        map_ui = wait_for(d.ui, lambda u: u["window"] not in ("", "flight_radial"))
        assert "map" in map_ui["window"]
        d.key("Escape")
        flight()
        passed("right release on a radial action opens the real map", window=map_ui["window"])

        empty_radial()
        release_on("radial.dock")
        d.command("accelerate", enabled=True, speed_multiplier=20, suppress_audio=True)
        docked = wait_for(d.ui, lambda u: u["window"] in ("spaceport", "mission_offer", "text_reader"), 60)
        passed("nearest-port radial action flies and docks normally", arrival_window=docked["window"])
        for _ in range(12):
            ui = d.ui()
            if ui["window"] == "spaceport":
                break
            if ui["window"] == "mission_offer":
                d.click("decline" if "decline" in ui["rects"] else "accept")
            elif ui["window"] == "text_reader":
                d.click("done")
            else:
                raise AssertionError(f"Unexpected landing dialog: {ui}")
        wait_for(d.ui, lambda u: u["window"] == "spaceport")
        d.click("launch")
        flight()
        # Leave must retire the docking goal in the game itself. Keep hands
        # off for about 40 simulated seconds so a stale goal would re-dock.
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            assert d.ui()["window"] == "", "Leave unexpectedly returned to a docked screen"
            assert d.get('/probe/automation')['phase'] == 'complete'
            time.sleep(.05)
        passed("Leave stays in space without auto-docking again")
        d.command("accelerate", enabled=False)
        ship_checked = False
        for _ in range(40):
            d.command("pause")
            state, view = d.state(), d.state("view")
            candidates = [(s, visible_point(s, state, view)) for s in d.state("ships")["ships"]]
            # The game deliberately clears targets already entering jump-out.
            candidates = [(s, p) for s, p in candidates if p and s["armor"] > 0 and s["hyperspace_jump_timer"] <= 0 and s['ai_state_code'] != 0x15]
            if not candidates:
                d.command("resume")
                d.command("accelerate", enabled=True, speed_multiplier=20, suppress_audio=True)
                time.sleep(0.1)
                continue
            d.command("accelerate", enabled=False)
            ship, point = candidates[0]
            d.mouse(*point, button="right", down=True)
            d.command("resume")
            ui = wait_for(d.ui, lambda u: u["window"] == "flight_radial")
            if "radial.board" in ui["rects"]:
                assert "radial.hail" in ui["rects"]
                (ARTIFACTS / "radial-ship.bmp").write_bytes(d.screenshot())
                release_on("radial.target")
                flight()
                selected = d.state("player")["primary_target_ship_slot"]
                assert selected == ship["slot"], {"expected": ship, "selected": selected, "point": point, "menu": ui}
                ship_checked = True
                passed("ship radial offers hail and board and selects the clicked ship", ship=ship["ship_class"])
                break
            release_on("cancel")
            flight()
        if not ship_checked:
            raise AssertionError("Could not exercise a visible ship radial")

        state, view = d.state(), d.state("view")
        planets = [(s, visible_point(s, state, view)) for s in d.state("stellars")["stellars"]]
        planets = [(s, p) for s, p in planets if p]
        assert planets, "No visible planet after launch"
        planet, point = planets[0]
        ui = radial(*point)
        assert "radial.dock" in ui["rects"] and "radial.hail" in ui["rects"]
        assert "radial.board" not in ui["rects"]
        (ARTIFACTS / "radial-planet.bmp").write_bytes(d.screenshot())
        release_on("radial.target")
        flight()
        assert d.state("travel")["selected_stellar_id"] == planet["id"]
        assert d.state("player")["primary_target_ship_slot"] == -1
        passed("planet radial targets the planet independently of a previous ship", planet=planet["name"])
        (ARTIFACTS / "flight-zoomed.bmp").write_bytes(d.screenshot())
        (ARTIFACTS / "modern-controls-results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    except BaseException:
        try:
            d.command("resume")
            (ARTIFACTS / "modern-controls-failure.bmp").write_bytes(d.screenshot())
            (ARTIFACTS / "modern-controls-failure.json").write_text(
                json.dumps({"passed": results, "ui": d.ui(), "state": d.state()}, indent=2), encoding="utf-8")
        finally:
            d.mouse(0, 0, button="left", down=False)
            d.mouse(0, 0, button="right", down=False)
        raise


if __name__ == "__main__":
    check()
