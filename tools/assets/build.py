"""build.py - build one asset inside Blender (docs/ASSETS.md).

    scripts/blender.sh -b --python tools/assets/build.py -- <gen.py> <out.glb>

Runs the generator's build() in an empty scene, exports the GLB with the
settings raylib 6.0's loader wants, then reads the file back and checks it:
one skin, no morph targets, no compression, the bone and triangle budgets.
Prints one "ASSET {...}" JSON line (the Makefile shows it), exits non-zero
on any failure, so a bad asset stops `make assets` instead of the headset.

build() may return a dict of budgets: {"tris": 2000, "bones": 128}.
"""
import json
import os
import runpy
import struct
import sys

import bpy

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sfq_assets  # noqa: E402

DEFAULT_BUDGET = {"tris": 3000, "bones": 128}

# raylib 6.0 (rmodels.c, LoadGLTF / LoadModelAnimationsGLTF): Y-up, one skin,
# TRS channels only (so sampled, no morphs), <= 4 influences, no Draco.
EXPORT = dict(
    export_format='GLB',
    export_yup=True,
    export_apply=True,
    export_texcoords=True,
    export_normals=True,
    export_tangents=False,
    export_materials='EXPORT',
    export_image_format='AUTO',
    export_vertex_color='MATERIAL',
    export_draco_mesh_compression_enable=False,
    export_animations=True,
    export_animation_mode='ACTIONS',
    export_force_sampling=True,
    export_frame_step=1,
    export_bake_animation=False,
    export_def_bones=True,
    export_hierarchy_flatten_bones=False,
    export_leaf_bone=False,
    export_anim_single_armature=True,
    export_reset_pose_bones=True,
    export_rest_position_armature=True,
    export_optimize_animation_size=False,
    export_morph=False,
    export_influence_nb=4,
    export_all_influences=False,
    export_skins=True,
    export_lights=False,
    export_cameras=False,
    export_extras=False,
    use_selection=False,
    use_visible=False,
)


def fail(msg):
    print("ASSET " + json.dumps({"ok": False, "error": msg}))
    sys.exit(1)


def read_glb(path):
    with open(path, 'rb') as f:
        magic, version, length = struct.unpack('<III', f.read(12))
        if magic != 0x46546C67:
            fail("not a GLB")
        clen, ctype = struct.unpack('<II', f.read(8))
        if ctype != 0x4E4F534A:
            fail("first chunk isn't JSON")
        return json.loads(f.read(clen).decode('utf-8')), length


def check(gltf, size, budget):
    prims = [p for m in gltf.get("meshes", []) for p in m.get("primitives", [])]
    tris = 0
    for p in prims:
        if "targets" in p:
            fail("a primitive has morph targets: raylib ignores them (export_morph is off; no shape keys)")
        mode = p.get("mode", 4)
        if mode != 4:
            fail(f"a primitive isn't triangles (mode {mode})")
        n = gltf["accessors"][p["indices"]]["count"] if "indices" in p else gltf["accessors"][p["attributes"]["POSITION"]]["count"]
        tris += n // 3
        for k in p["attributes"]:
            if k.startswith("JOINTS_") and k != "JOINTS_0":
                fail(f"{k}: more than four influences per vertex")
    if gltf.get("extensionsRequired"):
        fail("extensionsRequired " + ",".join(gltf["extensionsRequired"]) + ": raylib can't load it (Draco?)")
    skins = gltf.get("skins", [])
    if len(skins) > 1:
        fail(f"{len(skins)} skins: raylib takes one armature per file")
    bones = len(skins[0]["joints"]) if skins else 0
    if bones > budget["bones"]:
        fail(f"{bones} bones, budget {budget['bones']}")
    if tris > budget["tris"]:
        fail(f"{tris} triangles, budget {budget['tris']}")
    anims = [a.get("name", f"#{i}") for i, a in enumerate(gltf.get("animations", []))]
    images = len(gltf.get("images", []))
    return {"ok": True, "tris": tris, "meshes": len(gltf.get("meshes", [])), "primitives": len(prims),
            "materials": len(gltf.get("materials", [])), "bones": bones, "animations": anims,
            "images": images, "bytes": size}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) < 2:
        fail("usage: build.py -- <gen.py> <out.glb>")
    gen, out = argv[0], argv[1]
    sfq_assets.reset()
    mod = runpy.run_path(gen, run_name="asset")
    if "build" not in mod:
        fail(f"{gen} has no build()")
    budget = dict(DEFAULT_BUDGET)
    ret = mod["build"]()
    if isinstance(ret, dict):
        budget.update(ret)

    # the exporter's options differ a little between Blender releases: pass what this one has
    props = {p.identifier for p in bpy.ops.export_scene.gltf.get_rna_type().properties}
    args = {k: v for k, v in EXPORT.items() if k in props}
    for k in EXPORT:
        if k not in props:
            print(f"build.py: this Blender's exporter has no {k}; skipped", file=sys.stderr)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    if os.path.exists(out):
        os.remove(out)   # a failed build leaves no stale file behind for make to accept
    tmp = out[:-4] + ".part.glb"   # (the exporter appends .glb to any other name)
    bpy.ops.export_scene.gltf(filepath=tmp, **args)
    gltf, size = read_glb(tmp)
    info = check(gltf, size, budget)
    os.replace(tmp, out)
    info["file"] = out
    info["source"] = gen
    print("ASSET " + json.dumps(info))


main()
