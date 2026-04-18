"""
Build a low-poly underwater scene tailored for CS184's COLLADA parser.

Run headless:
  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --python scripts/build_underwater_scene.py

Produces: dae/underwater/wreck.dae

Design constraints
------------------
* CS184's COLLADA reader only supports <phong> materials with diffuse/emission
  colors (+ specular/index_of_refraction for glass/mirror). No textures, no
  PBR metallic-roughness, no normal maps.
* The participating medium in raytraced_renderer.cpp is bounded to a BBox
  (-1, 0, -1)..(1, 0.9, 1) with smoothstep fog falloff to y = 0.9. The scene
  is sized so the interesting geometry sits inside that volume.
* Low poly: every object is a primitive (cube / icosphere / cone) with no
  subdivision modifier to keep COLLADA export small and robust.
"""
import math
import bpy


# ---------------------------------------------------------------------------
# Helpers

def reset_scene():
    """Remove the default cube / camera / light and start clean."""
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in list(bpy.data.materials):
        bpy.data.materials.remove(block)
    for block in list(bpy.data.meshes):
        bpy.data.meshes.remove(block)
    for block in list(bpy.data.lights):
        bpy.data.lights.remove(block)
    for block in list(bpy.data.cameras):
        bpy.data.cameras.remove(block)


def _ensure_diffuse_bsdf(mat, color):
    """Rebuild the material's shader graph with a single Diffuse BSDF whose
    Color is `color`. Blender's COLLADA exporter reads this cleanly as
    <phong><diffuse>."""
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    d = nt.nodes.new("ShaderNodeBsdfDiffuse")
    d.inputs["Color"].default_value = (*color, 1.0)
    nt.links.new(d.outputs["BSDF"], out.inputs["Surface"])


def make_diffuse_material(name, color):
    """Lambertian material that exports as <phong><diffuse> with zero
    specular/emission. Readable by CS184's COLLADA parser."""
    mat = bpy.data.materials.new(name=name)
    _ensure_diffuse_bsdf(mat, color)
    mat.diffuse_color = (*color, 1.0)  # viewport + fallback
    mat.specular_intensity = 0.0
    return mat


def make_emissive_material(name, color, strength=5.0):
    """Mesh-light material: writes a non-zero <emission> channel that
    CS184 interprets as an area light. The `strength` multiplies color."""
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nt = mat.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emi = nt.nodes.new("ShaderNodeEmission")
    scaled = tuple(min(c * strength, 50.0) for c in color)
    emi.inputs["Color"].default_value = (*scaled, 1.0)
    emi.inputs["Strength"].default_value = 1.0
    nt.links.new(emi.outputs["Emission"], out.inputs["Surface"])
    # Blender's COLLADA exporter writes mat.diffuse_color into the <diffuse>
    # term AND mat.line_color... unfortunately the <emission> tag is driven
    # off the *material* 'emission'-style attributes, not the node graph.
    # We stuff the scaled color into both slots so at least one is honored.
    mat.diffuse_color = (*scaled, 1.0)
    try:
        # Newer Blender versions support material.metallic / material.emit
        # fields; older ones don't. Guard the assignment so it doesn't blow
        # up the script on whichever 3.x is installed.
        mat.emission_color = (*scaled, 1.0)  # type: ignore[attr-defined]
    except AttributeError:
        pass
    return mat


def assign(obj, mat):
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)


# ---------------------------------------------------------------------------
# Geometry

def build_seafloor(mat):
    # Deliberately oversized so rays heading toward the horizon still hit
    # the floor (the camera sits just outside a 3x3 plane and the edges
    # fell off-frame in earlier tests).
    bpy.ops.mesh.primitive_plane_add(size=12.0, location=(0, 0, 0))
    floor = bpy.context.object
    floor.name = "Seafloor"
    assign(floor, mat)
    return floor


def build_back_wall(mat):
    """A slanted back wall suggests a continental shelf / drop-off."""
    bpy.ops.mesh.primitive_plane_add(size=3.0, location=(0, -1.4, 0.75))
    wall = bpy.context.object
    wall.name = "BackWall"
    wall.rotation_euler = (math.radians(90), 0, 0)
    wall.scale = (1.0, 0.5, 1.0)
    assign(wall, mat)
    return wall


def build_rock(name, location, radius, mat):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=radius,
                                          location=location)
    rock = bpy.context.object
    rock.name = name
    # Squash along y so it reads as a boulder, not a sphere.
    rock.scale = (1.0, 0.7 + 0.2 * (hash(name) % 3), 0.85)
    rock.rotation_euler = (0, 0, math.radians(17 * (len(name) % 5)))
    assign(rock, mat)
    return rock


def build_wreck_hull(mat):
    """A simple elongated box representing a sunken fuselage."""
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-0.1, 0.1, 0.15))
    hull = bpy.context.object
    hull.name = "WreckHull"
    hull.scale = (0.85, 0.25, 0.22)
    hull.rotation_euler = (math.radians(-6), math.radians(12), math.radians(-18))
    assign(hull, mat)
    return hull


def build_wreck_wing(mat, flip=False):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(-0.1, 0.1, 0.15))
    wing = bpy.context.object
    wing.name = "WreckWing" + ("R" if flip else "L")
    wing.scale = (0.55, 0.05, 0.28)
    sign = -1 if flip else 1
    wing.rotation_euler = (math.radians(4), math.radians(12 + 8 * sign),
                           math.radians(-18))
    wing.location.x += 0.05 * sign
    wing.location.z += 0.02
    assign(wing, mat)
    return wing


def build_fish(name, location, color):
    """A stylised low-poly fish: stretched octahedron + tiny fin."""
    body_mat = make_diffuse_material(name + "_mat", color)
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.06,
                                          location=location)
    body = bpy.context.object
    body.name = name
    body.scale = (2.2, 0.7, 0.9)
    body.rotation_euler = (0, 0, math.radians(12 * (len(name) % 6)))
    assign(body, body_mat)
    return body


def build_seaweed(name, location, color):
    mat = make_diffuse_material(name + "_mat", color)
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    s = bpy.context.object
    s.name = name
    s.scale = (0.02, 0.02, 0.45)
    s.location.z = 0.45
    s.rotation_euler = (math.radians(4 * (hash(name) % 7)),
                        math.radians(3 * (hash(name) % 5)), 0)
    assign(s, mat)
    return s


def build_treasure_chest(location, mat_body, mat_gold):
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=location)
    chest = bpy.context.object
    chest.name = "ChestBody"
    chest.scale = (0.18, 0.13, 0.10)
    chest.rotation_euler = (0, 0, math.radians(25))
    assign(chest, mat_body)
    # A little gold cube on top hints at the lid.
    bpy.ops.mesh.primitive_cube_add(size=1.0,
                                    location=(location[0], location[1],
                                              location[2] + 0.11))
    lid = bpy.context.object
    lid.name = "ChestLid"
    lid.scale = (0.18, 0.13, 0.04)
    lid.rotation_euler = (0, 0, math.radians(25))
    assign(lid, mat_gold)
    return chest, lid


# ---------------------------------------------------------------------------
# Camera + lights

def build_camera():
    cam_data = bpy.data.cameras.new("Camera")
    cam_data.lens = 22.0  # wide enough for the full seafloor and wreck
    cam = bpy.data.objects.new("Camera", cam_data)
    bpy.context.collection.objects.link(cam)
    # Slight 3/4 angle looking down at the wreck; lifted enough to see the
    # seafloor, the rocks, the kelp, and the key light haze above.
    # 3/4 view tilted further down so the seafloor occupies the lower half
    # and the wreck / fish / backwall fill the upper half. Blender's COLLADA
    # export mirrors X and Y when writing node matrices, so we account for
    # that by placing the camera at the *pre-mirror* Blender position and
    # letting the exporter flip it to world coordinates.
    cam.location = (1.6, -1.8, 1.3)
    # Aim at the wreck origin but tilt the camera strongly downward so the
    # seafloor is framed in the lower third of the image.
    direction = (0.0 - cam.location.x, 0.3 - cam.location.y, -0.05 - cam.location.z)
    # Rotate to look at the target.
    import mathutils
    look = mathutils.Vector(direction).to_track_quat("-Z", "Y").to_euler()
    cam.rotation_euler = look
    bpy.context.scene.camera = cam
    return cam


def build_sun_plane(mat):
    """A small emissive plane near the top of the medium BBox simulates
    sunlight entering the water. Acts as an area light in CS184 via the
    <emission> channel."""
    bpy.ops.mesh.primitive_plane_add(size=0.35, location=(0.2, 0.0, 1.42))
    sun = bpy.context.object
    sun.name = "SunPanel"
    sun.rotation_euler = (math.radians(180), 0, 0)  # face down
    assign(sun, mat)
    return sun


def build_point_light():
    """Key point light. CS184's COLLADA reader writes <point> with quadratic
    attenuation — energy in scene units (not watts), tune modestly."""
    light_data = bpy.data.lights.new(name="Key", type="POINT")
    # Energy tuned for CS184's quadratic-attenuation point light at ~1.5 m;
    # diffuse reflectance up to ~0.7 means values ~3 tonemap around mid-grey.
    light_data.energy = 3.0
    light_data.color = (1.0, 0.97, 0.88)
    light = bpy.data.objects.new("Key", light_data)
    bpy.context.collection.objects.link(light)
    light.location = (0.2, -0.1, 1.55)
    return light


# ---------------------------------------------------------------------------
# Scene assembly

def build_scene():
    reset_scene()

    # Materials
    sand     = make_diffuse_material("Sand",     (0.72, 0.68, 0.55))
    cliff    = make_diffuse_material("Cliff",    (0.45, 0.42, 0.38))
    rock     = make_diffuse_material("Rock",     (0.35, 0.33, 0.30))
    rust     = make_diffuse_material("Rust",     (0.55, 0.30, 0.18))
    wing     = make_diffuse_material("Wing",     (0.40, 0.28, 0.20))
    wood     = make_diffuse_material("Wood",     (0.30, 0.18, 0.10))
    gold     = make_diffuse_material("Gold",     (0.95, 0.78, 0.22))
    seaweed  = (0.18, 0.42, 0.20)
    sun_mat  = make_emissive_material("Sun", (1.0, 0.98, 0.85), strength=5.0)

    # Ground + backdrop
    build_seafloor(sand)
    build_back_wall(cliff)

    # Rocks
    build_rock("Rock1", (-0.75, -0.35,  0.08), 0.20, rock)
    build_rock("Rock2", ( 0.85, -0.25,  0.10), 0.25, rock)
    build_rock("Rock3", (-0.35,  0.70,  0.06), 0.16, rock)
    build_rock("Rock4", ( 0.55,  0.55,  0.05), 0.12, rock)

    # Sunken wreck (hull + two wings)
    build_wreck_hull(rust)
    build_wreck_wing(wing, flip=False)
    build_wreck_wing(wing, flip=True)

    # Fish swimming mid-depth
    build_fish("Fish1", (-0.55, 0.10, 0.65), (0.85, 0.65, 0.25))
    build_fish("Fish2", ( 0.40, 0.35, 0.85), (0.20, 0.55, 0.75))
    build_fish("Fish3", ( 0.05, -0.45, 0.45), (0.75, 0.30, 0.30))

    # Seaweed clusters
    for i, (x, y) in enumerate([(-0.9, 0.2), (-0.7, 0.1), (0.9, 0.15),
                                (0.75, -0.3), (-0.2, 0.8), (0.15, 0.85)]):
        build_seaweed(f"Kelp{i}", (x, y, 0.0), seaweed)

    # Treasure chest next to the wreck
    build_treasure_chest((0.35, -0.05, 0.08), wood, gold)

    # Lighting: a single overhead point light simulates sunlight penetrating
    # the water. CS184's COLLADA parser only honors <library_lights>, not
    # emissive meshes, so we do not build an emissive sun plane here.
    build_point_light()

    # Camera
    build_camera()


def export_collada(path):
    import os
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # CS184's Camera uses Y-up internally (its auto-camera builds a basis
    # with upVec = (0, 1, 0) in world space), and the reference dae/sky
    # scenes, despite declaring Z_UP, are actually authored with Y as the
    # vertical axis (CBspheres' floor vertices are all y == 0). So we
    # convert Blender's native +Z up into world +Y on export.
    bpy.ops.wm.collada_export(
        filepath=path,
        apply_modifiers=True,
        selected=False,
        include_children=True,
        include_armatures=False,
        include_shapekeys=False,
        triangulate=True,
        use_texture_copies=False,
        export_global_forward_selection="-Z",
        export_global_up_selection="Y",
    )


def dump_cam_settings(path, cam_pos_blender, target_blender,
                      screen_w=480, screen_h=360, hfov_deg=55.0,
                      near_clip=0.1, far_clip=1000.0):
    """Write a cam_settings.txt file that PathTracer::load_settings will
    consume via the `-a` flag. This bypasses the auto-camera placement
    (which is based on scene-bbox spherical angles and gives unreliable
    framings on custom scenes).

    Converts the given Blender-space positions (Z-up) to the world space
    (Y-up) that PathTracer expects, matching the export_collada() axis map.
    """
    import math as _m
    import os as _os

    def b2w(p):
        # Blender (+X, +Y, +Z up) -> World (+X, -Z, +Y) under our -Z/+Y export.
        return (p[0], p[2], -p[1])

    pos = b2w(cam_pos_blender)
    target = b2w(target_blender)

    dir_to_cam = (pos[0] - target[0], pos[1] - target[1], pos[2] - target[2])
    r = _m.sqrt(sum(c * c for c in dir_to_cam))
    dir_unit = tuple(c / r for c in dir_to_cam)

    phi = _m.acos(max(-1.0, min(1.0, dir_unit[1])))
    theta = _m.atan2(dir_unit[0], dir_unit[2])

    # Build c2w to match Camera::compute_position's convention:
    #   col 0 (screenX) = normalize(cross((0,1,0), dirToCamera))
    #   col 1 (screenY) = normalize(cross(dirToCamera, col 0))
    #   col 2 (back)    = dirToCamera.unit()
    up = (0.0, 1.0, 0.0)

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0])

    def norm(v):
        n = _m.sqrt(sum(c * c for c in v))
        return (v[0] / n, v[1] / n, v[2] / n) if n > 1e-12 else (1.0, 0.0, 0.0)

    sx = norm(cross(up, dir_to_cam))
    sy = norm(cross(dir_to_cam, sx))
    sz = dir_unit

    # Aspect / FOV derivation mirrors Camera::set_screen_size():
    #   hFov = 2 * atan(screenW / (2 * screenDist))
    # so we solve for screenDist from the given hFov.
    hfov = _m.radians(hfov_deg)
    screen_dist = screen_w / (2.0 * _m.tan(hfov / 2.0))
    vfov_rad = 2.0 * _m.atan(screen_h / (2.0 * screen_dist))
    vfov = _m.degrees(vfov_rad)
    ar = screen_w / screen_h
    min_r = r * 0.1
    max_r = r * 10.0

    lines = []
    lines.append(f"{hfov_deg:.6f} {vfov:.6f} {ar:.6f} {near_clip:.6f} {far_clip:.6f}")
    lines.append(f"{pos[0]:.6f} {pos[1]:.6f} {pos[2]:.6f} "
                 f"{target[0]:.6f} {target[1]:.6f} {target[2]:.6f}")
    lines.append(f"{phi:.6f} {theta:.6f} {r:.6f} {min_r:.6f} {max_r:.6f}")
    # c2w layout in dump_settings is row-major; sx/sy/sz are the three
    # columns, so we need to interleave by row.
    c2w_vals = [sx[0], sy[0], sz[0],
                sx[1], sy[1], sz[1],
                sx[2], sy[2], sz[2]]
    lines.append(" ".join(f"{v:.6f}" for v in c2w_vals))
    lines.append(f"{screen_w} {screen_h} {screen_dist:.6f}")
    lines.append(f"{r:.6f} 0.0")  # focalDistance, lensRadius (pinhole)

    _os.makedirs(_os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"[underwater] wrote {path}")


if __name__ == "__main__":
    build_scene()
    out = "dae/underwater/wreck.dae"
    export_collada(out)
    print(f"[underwater] exported {out}")
    # Also emit a matching cam_settings.txt that pathtracer can consume
    # via `-a` to bypass the unreliable auto-camera placement.
    dump_cam_settings(
        "dae/underwater/wreck_cam.txt",
        # Blender-space camera positions (X right, Y forward, Z up).
        # Stand ~2 m back, slightly above the seafloor, tilted slightly down.
        cam_pos_blender=(0.0, -2.6, 0.9),
        target_blender=(0.0, 0.0, 0.35),
        screen_w=480, screen_h=360, hfov_deg=55.0,
    )
