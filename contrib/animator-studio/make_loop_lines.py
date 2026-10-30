"""Writes Extension/AnimationStudio/loop_lines.h: the game's manual and grind loops, each with the clips that go into it
and the clips that come out of it on the same line (the same stance and way in), for the studio's COPY TO INS AND OUTS.

    python -I make_loop_lines.py <ReSkate.Tricks folder> <loop_lines.h to write>

Grinds are matched by their poses in the trick library the studio builds (ReSkate.Tricks/library): an "into" clip whose
last frame is the loop's first, and a popout whose first frame is the loop's last (both within 3 degrees on average over
the body's main joints; the game's own lines meet within 0.5). Manuals go by their names: the game's manual "into" and
landing clips end on the loop's first frame, its exits and brakes start near its last, and its reverts start on the other
stance's loop (left out: a line never changes stance).
"""
import json
import struct
import sys

BODY = [7, 8, 9, 10, 341, 342, 343, 42, 44, 101, 103, 46, 47, 48, 275, 276, 277]
GRIND = 'c_proto_onb_grind_'
MANUAL = 'c_proto_onb_manual_'


def read_clip(path):
    """First and last frame's joint rotations (xyzw) of a studio clip (FAS1/2/3)."""
    b = open(path, 'rb').read()
    magic = b[:4]
    joints, frames = struct.unpack_from('<II', b, 4)
    at = 20
    has_turned = False
    if magic == b'FAS3':
        flags = b[at]
        at += 1
        has_turned = bool(flags & 2)
    at += 4 * frames
    if magic == b'FAS3':
        if has_turned:
            at += joints
        moves = b[at:at + joints]
        at += joints
        still = {}
        for j in range(joints):
            if not moves[j]:
                still[j] = struct.unpack_from('<10f', b, at)
                at += 40
        moving = [j for j in range(joints) if moves[j]]
        per = len(moving) + (1 if flags & 1 else 0)

        def frame(f):
            out = dict(still)
            base = at + f * per * 40
            for i, j in enumerate(moving):
                out[j] = struct.unpack_from('<10f', b, base + i * 40)
            return out
    else:
        def frame(f):
            base = at + f * joints * 40
            return {j: struct.unpack_from('<10f', b, base + j * 40) for j in range(joints)}
    return frame(0), frame(frames - 1)


def apart(p, q):
    """Average turn (degrees) between two frames over the body's main joints."""
    import math
    total = 0.0
    for j in BODY:
        dot = abs(sum(p[j][3 + i] * q[j][3 + i] for i in range(4)))
        total += math.degrees(2 * math.acos(min(1.0, dot)))
    return total / len(BODY)


def main(tricks, out_path):
    lib = tricks.rstrip('/\\') + '/library/'
    catalog = json.load(open(lib + 'catalog.json', encoding='utf-8'))
    files = {e['asset'].split('/')[-1]: e['file'] for e in catalog}
    ends = {}

    def end(name):
        if name not in ends:
            ends[name] = read_clip(lib + files[name])
        return ends[name]

    lines = []
    grinds = sorted(n for n in files if n.startswith(GRIND) and '_rail_' in n)
    intos = [n for n in grinds if '_rail_into' in n]
    popouts = [n for n in grinds if 'popout' in n]
    for loop in (n for n in grinds if '_rail_loop' in n):
        first, last = end(loop)
        ins = [n for n in intos if apart(end(n)[1], first) < 3.0]
        outs = [n for n in popouts if apart(end(n)[0], last) < 3.0]
        lines.append((loop, ins, outs))
    for family in ('t_', 't_fakie_', 'n_', 'n_fakie_'):
        loop = MANUAL + family + 'loop_med'
        if loop not in files:
            continue
        ins = [MANUAL + family + n for n in ('into', 'land_addi') if MANUAL + family + n in files]
        outs = [MANUAL + family + n for n in ('exit', 'brake_rolling_slow') if MANUAL + family + n in files]
        lines.append((loop, ins, outs))
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('#pragma once\n// Written by contrib/animator-studio/make_loop_lines.py: see there for how the lines are found.\n')
        f.write('#include <array>\n#include <string_view>\n\nnamespace dingosdk::anim_studio {\n')
        f.write('// A loop of the game\'s (its clip\'s name after animation/dingo/), the clips that go into it and those that come out\n'
                '// of it on its line, each list split by |.\n')
        f.write('struct LoopLine {\n    std::string_view loop, ins, outs;\n};\n')
        f.write('inline constexpr std::array<LoopLine, %d> loop_lines{{\n' % len(lines))
        for loop, ins, outs in lines:
            f.write('    {"%s", "%s", "%s"},\n' % (loop, '|'.join(ins), '|'.join(outs)))
        f.write('}};\n} // namespace dingosdk::anim_studio\n')
    print('%d lines written to %s' % (len(lines), out_path))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
