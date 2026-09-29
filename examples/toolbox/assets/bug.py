# bug.py - the magenta bug from Daddy Bug Smasher's sprite sheet, in 3D
# (examples/toolbox/resources/models/bug.glb; docs/ASSETS.md).
#
# The drawing: a fat pink body, a small pale-green head low at the front with
# a dot of an eye, one antenna curving up and back over the body, and thin
# black legs. This keeps those proportions and adds what a side view can't
# show: a second eye, and six legs (three a side) that walk.
#
# Size: about 1.1 m nose to tail, feet on the ground, facing +Y here (-Z in
# the game). The garden scales it to a bug's radius. Rig: a body bone, a
# head bone (eyes and antenna ride on it), one bone per leg. Animations:
# "walk" (a tripod gait, 0.8 s loop) and "idle" (breathing, 2 s loop).
import math

from sfq_assets import Action, arc, armature, cylinder, group, join, material, rgb, skin, sphere, tube, world_rotation

# from the sprite, as raylib colors (see sfq_assets on why they aren't linearized)
PINK = rgb(247, 166, 242)
PINK_DARK = rgb(196, 110, 190)
GREEN = rgb(200, 245, 168)
BLACK = rgb(32, 24, 32)
WHITE = rgb(245, 245, 245)

BODY_C = (0.0, -0.05, 0.42)        # the body's center (x, y, z): z is up here
BODY_R = 0.36
HEAD_C = (0.0, 0.50, 0.30)
HEAD_R = 0.15

# legs: (name, hip, knee, foot); the leg bone runs hip -> foot
LEGS = []
for side, sx in (("l", -1), ("r", 1)):
    for row, y in (("f", 0.25), ("m", 0.0), ("b", -0.28)):
        hip = (sx * 0.22, y, 0.28)
        knee = (sx * 0.44, y + 0.02, 0.30)
        foot = (sx * 0.52, y, 0.0)
        LEGS.append((f"leg_{row}{side}", hip, knee, foot))


def build():
    pink = material("bug_pink", PINK)
    pink_dark = material("bug_pink_dark", PINK_DARK)
    green = material("bug_green", GREEN)
    black = material("bug_black", BLACK, roughness=0.6)
    white = material("bug_white", WHITE, roughness=0.4)

    parts = []
    # the body: an egg, fatter and higher at the back, and a darker underside
    body = sphere("body", BODY_R, at=BODY_C, scale=(1.0, 1.35, 0.85), segments=14, rings=9, mat=pink)
    parts.append(group(body, "body"))
    belly = sphere("belly", BODY_R * 0.92, at=(BODY_C[0], BODY_C[1], BODY_C[2] - 0.08),
                   scale=(1.0, 1.3, 0.55), segments=12, rings=6, mat=pink_dark)
    parts.append(group(belly, "body"))

    # the head, low at the front, tucked against the body
    parts.append(group(sphere("head", HEAD_R, at=HEAD_C, segments=12, rings=8, mat=green), "head"))
    # eyes on both sides, looking a little forward
    for sx in (-1, 1):
        ex = HEAD_C[0] + sx * HEAD_R * 0.72
        eye = (ex, HEAD_C[1] + HEAD_R * 0.45, HEAD_C[2] + HEAD_R * 0.25)
        parts.append(group(sphere(f"eye_{sx}", HEAD_R * 0.30, at=eye, segments=8, rings=6, mat=white), "head"))
        pupil = (ex + sx * HEAD_R * 0.16, eye[1] + HEAD_R * 0.16, eye[2] + HEAD_R * 0.04)
        parts.append(group(sphere(f"pupil_{sx}", HEAD_R * 0.14, at=pupil, segments=6, rings=4, mat=black), "head"))
    # one antenna from the top of the head, curving up and back over the body
    pts = arc((HEAD_C[0], HEAD_C[1] - 0.30, HEAD_C[2] + 0.10), 0.34, -10, 110, 7, plane='YZ')
    parts.append(group(tube("antenna", pts, 0.014, segments=5, mat=black), "head"))
    parts.append(group(sphere("antenna_tip", 0.03, at=pts[-1], segments=6, rings=4, mat=black), "head"))

    # six thin legs: hip -> knee -> foot
    for name, hip, knee, foot in LEGS:
        upper = cylinder(f"{name}_upper", hip, knee, 0.024, segments=6, mat=black)
        lower = cylinder(f"{name}_lower", knee, foot, 0.020, radius_b=0.012, segments=6, mat=black)
        parts.append(group(join(name, [upper, lower]), name))

    bug = join("bug", parts)

    # the rig: body -> head; body -> each leg
    bones = [("body", (0, -0.35, 0.42), (0, 0.30, 0.42), None),
             ("head", (0, 0.36, 0.30), (0, 0.62, 0.30), "body")]
    for name, hip, knee, foot in LEGS:
        bones.append((name, hip, foot, "body"))
    arm = armature("bug_rig", bones)
    skin(bug, arm)

    # --- walk: a tripod gait. Legs fl, mr, bl swing together, fr, ml, br opposite.
    # A leg points sideways-down, so stepping is a turn about the world Z axis
    # (the foot moves fore-aft) with a lift about the world Y axis on the swing.
    L = 24   # frames at 30 fps: 0.8 s per cycle
    swing = 22.0   # degrees fore/aft
    lift = 14.0
    with Action(arm, "walk", L) as a:
        for name, hip, knee, foot in LEGS:
            side = 1 if name.endswith("r") else -1
            tripod = 0 if name[4:] in ("fl", "mr", "bl") else 1
            # a foot on the right steps forward when its bone turns the other way about Z
            fwd = -side
            phase = tripod * (L // 2)
            for k, f in enumerate((1, 1 + L // 4, 1 + L // 2, 1 + 3 * L // 4, L + 1)):
                fr = ((f - 1 + phase) % L) + 1
                t = k / 4.0 * 2 * math.pi
                yaw = fwd * swing * math.cos(t)                    # back ... forward ... back
                up = max(0.0, math.sin(t)) * lift * (-side)         # lifted during the forward swing only
                # the two turns combined: about Z, then the lift
                q = world_rotation(arm, name, 'Z', yaw) @ world_rotation(arm, name, 'Y', up)
                a.rot(name, fr, q)
        # the body bobs twice a cycle and the head nods along
        for f, z in ((1, 0.0), (1 + L // 4, 0.025), (1 + L // 2, 0.0), (1 + 3 * L // 4, 0.025), (L + 1, 0.0)):
            a.move("body", f, (0, 0, z))
        for f, deg in ((1, 0.0), (1 + L // 4, -4.0), (1 + L // 2, 0.0), (1 + 3 * L // 4, -4.0), (L + 1, 0.0)):
            a.turn("head", f, 'X', deg)

    # --- idle: breathing, and the head looking about
    I = 60
    with Action(arm, "idle", I) as a:
        for f, z in ((1, 0.0), (1 + I // 2, 0.012), (I + 1, 0.0)):
            a.move("body", f, (0, 0, z))
        for f, deg in ((1, 0.0), (1 + I // 4, 12.0), (1 + I // 2, 0.0), (1 + 3 * I // 4, -12.0), (I + 1, 0.0)):
            a.turn("head", f, 'Z', deg)

    return {"tris": 2500, "bones": 16}
