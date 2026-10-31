"""Clip Check: how deep each shoe and shin goes into the board, frame by frame, from an Animator Studio bug report.

    python clip_check.py <report .zip or its unpacked dump folder> [--all]

Reads the dump's feet.csv (every update: the hips, legs, feet and toes, and the board, as the game had them and as they
were drawn) and timeline.csv (what the studio was doing). For every frame it measures each shoe (heel, middle, ball and
toe on both edges, and the upper) and shin against the deck, its kicks, nose, tail, trucks and wheels, from the side the
hips are on, for the pose drawn and for the game's own pose. It prints the frames where the drawn shoe is deeper in the
drawn board than the game's own is in the game's board, grouped into runs, with the trick, the clock and the weight.

The measurements (shoe and deck) are the ones taken from the game's own clips on 2026-10-28: the ankle stands 0.105 m
over the board's mount, the toe joint 0.044 m, the deck's grip 0.019 m over the mount.
"""
import csv
import io
import math
import sys
import zipfile
from collections import defaultdict
from pathlib import Path

# ---- maths ----


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def rot(q, v):
    x, y, z, w = q
    p = qmul(qmul(q, (v[0], v[1], v[2], 0.0)), (-x, -y, -z, w))
    return p[:3]


def inv_rot(q, v):
    x, y, z, w = q
    return rot((-x, -y, -z, w), v)


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


# ---- the shoe and the deck (Extension/AnimationStudio: the same numbers) ----
DOWN = (0.5408, 0.8412, 0.0)
FORWARD = (0.8412, -0.5408, 0.0)
ANKLE_OVER_SOLE = 0.085


def shoe_point(forward, side, over_sole):
    d = ANKLE_OVER_SOLE - over_sole
    return (DOWN[0] * d + FORWARD[0] * forward, DOWN[1] * d + FORWARD[1] * forward, side)


SOLE = [shoe_point(f, s, o) for f, s, o in [(-0.065, 0.035, 0), (-0.065, -0.035, 0), (0.015, 0.045, 0), (0.015, -0.045, 0),
                                              (0.094, 0.05, 0), (0.094, -0.05, 0), (0.165, 0.03, 0.008), (0.165, -0.03, 0.008),
                                              (0.175, 0, 0.012)]]
UPPER = [shoe_point(f, s, o) for f, s, o in [(0.15, 0, 0.035), (0.07, 0, 0.065), (-0.07, 0, 0.045), (-0.02, 0, 0.1),
                                               (0.03, 0.052, 0.03), (0.03, -0.052, 0.03), (0.11, 0.045, 0.03)]]
SHIN = [0.14, 0.24, 0.34]
SHIN_RADIUS = 0.07
TOP, THICK, TIP, HALF = 0.019, 0.014, 0.40, 0.105
KICK_FROM, KICK_RISE, ROUND_FROM = 0.29, 0.045, 0.30
TRUCKS_AT, TRUCKS_HALF_LEN, TRUCKS_HALF_W, WHEELS = 0.18, 0.04, 0.115, -0.087
MIDDLE = TOP - THICK / 2


def deck_top(a):
    x = min(max((abs(a) - KICK_FROM) / (TIP - KICK_FROM), 0.0), 1.0)
    return TOP + KICK_RISE * x * x


def half_width(a):
    a = abs(a)
    if a <= ROUND_FROM:
        return HALF
    if a >= TIP:
        return 0.0
    x = (a - ROUND_FROM) / (TIP - ROUND_FROM)
    return HALF * math.sqrt(max(0.0, 1 - x * x))


def depth(a, up, w, top, radius=0.0):
    """How deep a point (along, up, across in the board's frame) is in the board from the side the feet are on."""
    margin = 0.01 if top and radius == 0 else 0.0
    if abs(a) > TIP + margin or abs(w) > half_width(min(abs(a), TIP)) + margin:
        return 0.0
    if top:
        d = deck_top(a) - (up - radius)
    else:
        trucks = abs(abs(a) - TRUCKS_AT) < TRUCKS_HALF_LEN and abs(w) < TRUCKS_HALF_W
        d = (up + radius) - (WHEELS if trucks else deck_top(a) - THICK)
    return d if 0 < d <= 0.3 else 0.0


# The board's mount frame: Y up, Z along (tail to nose), X across (measured in the game's clips).
def on_board(board, point):
    q, p = board
    local = inv_rot(q, sub(point, p))
    return local[2], local[1], local[0]


def leg_depth(joints, board, foot, knee, hips_top):
    """The deepest point of one leg (shoe, upper and shin) in the board, and which part it is."""
    fq, fp = joints[foot]
    worst, part = 0.0, ""
    for name, pts in (("sole", SOLE), ("shoe", UPPER)):
        for pt in pts:
            a, up, w = on_board(board, add(fp, rot(fq, pt)))
            d = depth(a, up, w, hips_top)
            if d > worst:
                worst, part = d, name
    kq, kp = joints[knee]
    for along in SHIN:
        a, up, w = on_board(board, add(kp, rot(kq, (along, 0, 0))))
        d = depth(a, up, w, hips_top, SHIN_RADIUS)
        if d > worst:
            worst, part = d, "shin"
    return worst, part


LEGS = {"left": (343, 342), "right": (10, 9)}
HIPS = 7


def read_dump(path):
    """The dump's files as text, from a folder or a zip (a Report a bug zip has the dump inside a folder)."""
    path = Path(path)
    files = {}
    if path.is_dir():
        for p in path.rglob("*.csv"):
            files[p.name] = p.read_text(encoding="utf-8", errors="replace")
    else:
        with zipfile.ZipFile(path) as z:
            for name in z.namelist():
                if name.endswith(".csv"):
                    files[Path(name).name] = z.read(name).decode("utf-8", errors="replace")
    return files


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    files = read_dump(sys.argv[1])
    show_all = "--all" in sys.argv
    if "feet.csv" not in files:
        print("This report has no feet.csv: it was made by a build before the feet track (deck guard test 8 / 1.0.13 base).")
        return
    timeline = {}
    for row in csv.DictReader(io.StringIO(files["timeline.csv"])):
        timeline[row["sequence"]] = row
    frames = defaultdict(lambda: {"drawn": {}, "native": {}})
    for row in csv.DictReader(io.StringIO(files["feet.csv"])):
        q = (float(row["qx"]), float(row["qy"]), float(row["qz"]), float(row["qw"]))
        p = (float(row["px"]), float(row["py"]), float(row["pz"]))
        frames[(float(row["seconds"]), row["sequence"])][row["stage"]][int(row["joint"])] = (q, p)
    BOARD = 900
    bad = []
    for (seconds, seq), f in sorted(frames.items(), key=lambda kv: kv[0][0]):
        drawn, native = f["drawn"], f["native"]
        if BOARD not in drawn or HIPS not in drawn:
            continue
        top = on_board(drawn[BOARD], drawn[HIPS][1])[1] > MIDDLE
        native_top = BOARD in native and HIPS in native and on_board(native[BOARD], native[HIPS][1])[1] > MIDDLE
        for leg, (foot, knee) in LEGS.items():
            if foot not in drawn or knee not in drawn:
                continue
            d, part = leg_depth(drawn, drawn[BOARD], foot, knee, top)
            own = 0.0
            if BOARD in native and foot in native and knee in native:
                own, _ = leg_depth(native, native[BOARD], foot, knee, native_top)
            if show_all or d > own + 0.005:
                t = timeline.get(seq, {})
                bad.append((seconds, leg, d, own, part, t))
    if not bad:
        print("No shoe or shin went into the board deeper than the game's own pose has it. Frames read:", len(frames))
        return
    # Runs of frames close together, the deepest of each.
    runs = []
    for item in bad:
        if runs and item[0] - runs[-1][-1][0] < 0.1:
            runs[-1].append(item)
        else:
            runs.append([item])
    print(f"{len(bad)} frames with a shoe or shin in the board (deeper than the game's own), in {len(runs)} runs:\n")
    for run in runs:
        deepest = max(run, key=lambda x: x[2] - x[3])
        s, leg, d, own, part, t = deepest
        print(f"{run[0][0]:7.3f}s to {run[-1][0]:7.3f}s ({len(run)} frames): deepest {d * 100:4.1f} cm ({leg} {part}; the game's own "
              f"{own * 100:3.1f} cm) at {s:.3f}s")
        print(f"    trick {t.get('trick', '?')!s:14} custom {t.get('preset', '')!s:28} phase {t.get('observed_phase', '?'):8} "
              f"physics {t.get('physics', '?'):4} clip {t.get('clip_time', '?')[:5]:5} weight {t.get('weight', '?')[:4]:4} "
              f"clock {t.get('clock_state', '?')}")


if __name__ == "__main__":
    main()
