"""sfq_assets - helpers for asset generators (docs/ASSETS.md).

An asset is a Python file with a build() function. It makes the thing in an
empty Blender scene with these helpers; tools/assets/build.py runs it inside
Blender (headless, scripts/blender.sh), exports the GLB the way raylib 6.0
loads it, and checks the budgets. The .py is the source; the .glb is built.

Conventions:
- Meters. Model in Blender's usual Z-up with the ground at z = 0; the
  exporter turns it into glTF's Y-up. Face +Y in Blender: that comes out as
  -Z in the file, "forward" in sfxr's world.
- Colors are given as the 0..255 values you'd hand raylib. They go into the
  material's base color as they are (no sRGB-to-linear conversion), because
  raylib reads the factor straight into a Color and draws gamma-space colors
  raw (CLAUDE.md, "Color"). Blender's own viewport would show them lighter;
  we never look at it.
- One armature per file, every deform bone weighted, at most 128 bones and
  four influences per vertex, no shape keys: raylib's glTF loader.
- Parts are joined into one mesh with a vertex group per bone, so the file
  has one skin (raylib takes the first skin only).
"""
import math

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

FPS = 30


# --- scene ------------------------------------------------------------------------------

def reset():
    """An empty scene at 30 fps, meters."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s = bpy.context.scene
    s.render.fps = FPS
    s.unit_settings.system = 'METRIC'
    s.unit_settings.scale_length = 1.0
    s.frame_start = 1
    s.frame_end = 1


def rgb(r, g, b):
    """A raylib-style color (0..255 each) as the tuple materials take."""
    return (r / 255.0, g / 255.0, b / 255.0)


def material(name, color, roughness=0.85, metallic=0.0, emission=None):
    """A plain material: base color (see the module notes), no textures."""
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metallic
    if emission is not None:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1.0)
        bsdf.inputs["Emission Strength"].default_value = 1.0
    m.diffuse_color = (*color, 1.0)
    return m


# --- meshes -----------------------------------------------------------------------------

def _object(name, bm, mat, smooth):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    if mat is not None:
        me.materials.append(mat)
    for p in me.polygons:
        p.use_smooth = smooth
    return ob


def sphere(name, radius, at=(0, 0, 0), scale=(1, 1, 1), segments=12, rings=8, mat=None, smooth=True):
    """A UV sphere (or an ellipsoid with scale), centered at `at`."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=rings, radius=radius)
    bmesh.ops.scale(bm, vec=Vector(scale), verts=bm.verts)
    bmesh.ops.translate(bm, vec=Vector(at), verts=bm.verts)
    return _object(name, bm, mat, smooth)


def cylinder(name, a, b, radius, radius_b=None, segments=6, mat=None, smooth=True, caps=True):
    """A cylinder (a cone if radius_b differs) from point a to point b."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=caps, cap_tris=True, segments=segments,
                          radius1=radius, radius2=radius if radius_b is None else radius_b, depth=d.length)
    rot = Vector((0, 0, 1)).rotation_difference(d.normalized())
    bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=rot.to_matrix(), verts=bm.verts)
    bmesh.ops.translate(bm, vec=(a + b) / 2, verts=bm.verts)
    return _object(name, bm, mat, smooth)


def box(name, size, at=(0, 0, 0), mat=None):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector(size), verts=bm.verts)
    bmesh.ops.translate(bm, vec=Vector(at), verts=bm.verts)
    return _object(name, bm, mat, False)


def tube(name, points, radius, segments=5, mat=None):
    """A polyline of cylinders through `points` (an antenna, a tail, a cord)."""
    parts = [cylinder(f"{name}.{i}", points[i], points[i + 1], radius, segments=segments, mat=mat, caps=True)
             for i in range(len(points) - 1)]
    return join(name, parts)


def arc(center, radius, start_deg, end_deg, n, plane='YZ'):
    """Points along a circular arc, for tube(): plane 'XY', 'YZ' or 'XZ'."""
    out = []
    for i in range(n + 1):
        t = math.radians(start_deg + (end_deg - start_deg) * i / n)
        u, v = radius * math.cos(t), radius * math.sin(t)
        c = Vector(center)
        if plane == 'YZ':
            out.append(c + Vector((0, u, v)))
        elif plane == 'XZ':
            out.append(c + Vector((u, 0, v)))
        else:
            out.append(c + Vector((u, v, 0)))
    return out


def group(ob, bone):
    """Every vertex of `ob` fully in vertex group `bone` (a rigid part on that bone).
    Groups survive join(), so parts can be grouped before joining."""
    vg = ob.vertex_groups.get(bone) or ob.vertex_groups.new(name=bone)
    vg.add(list(range(len(ob.data.vertices))), 1.0, 'REPLACE')
    return ob


def join(name, objects):
    """One object out of several (materials and vertex groups kept)."""
    objects = [o for o in objects if o is not None]
    if len(objects) == 1:
        objects[0].name = name
        return objects[0]
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    with bpy.context.temp_override(active_object=objects[0], selected_editable_objects=objects,
                                   selected_objects=objects):
        bpy.ops.object.join()
    ob = objects[0]
    ob.name = name
    ob.data.name = name
    return ob


def set_origin(ob, at=(0, 0, 0)):
    """Move the object's origin to `at` (the mesh stays where it is)."""
    off = Vector(at) - ob.location
    ob.data.transform(Matrix.Translation(-off))
    ob.location = Vector(at)


# --- rigs --------------------------------------------------------------------------------

def armature(name, bones):
    """An armature from (bone, head, tail, parent-or-None) rows; all deform bones."""
    arm = bpy.data.armatures.new(name)
    ob = bpy.data.objects.new(name, arm)
    bpy.context.collection.objects.link(ob)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode='EDIT')
    for bone, head, tail, parent in bones:
        eb = arm.edit_bones.new(bone)
        eb.head, eb.tail = Vector(head), Vector(tail)
        eb.use_deform = True
        if parent:
            eb.parent = arm.edit_bones[parent]
    bpy.ops.object.mode_set(mode='OBJECT')
    return ob


def skin(ob, arm):
    """Deform `ob` with `arm` through its vertex groups (named after bones)."""
    mod = ob.modifiers.new("Armature", 'ARMATURE')
    mod.object = arm
    ob.parent = arm
    for b in arm.data.bones:
        if b.use_deform and ob.vertex_groups.get(b.name) is None:
            ob.vertex_groups.new(name=b.name)   # a bone that moves nothing is still a bone
    return ob


def world_rotation(arm, bone, axis, degrees):
    """The pose-space rotation that turns `bone` about a world axis ('X', 'Y', 'Z')
    by `degrees`, around its head. Pose rotations are in the bone's own rest
    frame, which is rarely the axis you're thinking in; this does the change of
    basis (the bone's rest frame in armature space, assuming its parents are
    unrotated in the pose, which is how the animations here are written)."""
    m = arm.data.bones[bone].matrix_local.to_3x3()
    r = Matrix.Rotation(math.radians(degrees), 3, axis)
    return (m.inverted() @ r @ m).to_quaternion()


class Action:
    """Keyframes for one animation: with Action(arm, "walk", frames) as a: a.rot(...)."""

    def __init__(self, arm, name, length, loop=True):
        self.arm, self.name, self.length, self.loop = arm, name, length, loop
        self.act = bpy.data.actions.new(name)
        if arm.animation_data is None:
            arm.animation_data_create()
        arm.animation_data.action = self.act
        # Blender 4.4+ actions have slots; keying through the pose bones picks one
        for pb in arm.pose.bones:
            pb.rotation_mode = 'QUATERNION'
            pb.rotation_quaternion = Quaternion()
            pb.location = Vector((0, 0, 0))

    def __enter__(self):
        return self

    def rot(self, bone, frame, q):
        pb = self.arm.pose.bones[bone]
        pb.rotation_quaternion = q
        pb.keyframe_insert('rotation_quaternion', frame=frame)

    def turn(self, bone, frame, axis, degrees):
        """Rotate `bone` about a world axis by `degrees` at `frame` (see world_rotation)."""
        self.rot(bone, frame, world_rotation(self.arm, bone, axis, degrees))

    def move(self, bone, frame, xyz):
        pb = self.arm.pose.bones[bone]
        pb.location = Vector(xyz)
        pb.keyframe_insert('location', frame=frame)

    def __exit__(self, *exc):
        self.act.use_frame_range = True
        self.act.frame_start, self.act.frame_end = 1, self.length
        if self.loop:
            self.act.use_cyclic = True
        # every action goes on its own NLA track: that's how the glTF exporter
        # finds them all (export_animation_mode ACTIONS); the active one is cleared
        self.arm.animation_data.action = None
        track = self.arm.animation_data.nla_tracks.new()
        track.name = self.name
        strip = track.strips.new(self.name, 1, self.act)
        strip.name = self.name
        for pb in self.arm.pose.bones:
            pb.rotation_quaternion = Quaternion()
            pb.location = Vector((0, 0, 0))
        s = bpy.context.scene
        s.frame_end = max(s.frame_end, self.length)
        return False
