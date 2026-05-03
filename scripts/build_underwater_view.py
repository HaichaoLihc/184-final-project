#!/usr/bin/env python3
"""Produce CBspheres_underwater.dae from CBspheres_lambertian.dae by retargeting
the camera into an "underwater diver looking up at the surface" pose, and
shrinking + repositioning the area light into a corner of the ceiling so the
camera-light geometry actually exercises forward-scattering HG (light from
upper-left vs. camera looking up-and-forward gives non-trivial cos θ).

CBspheres_lambertian.dae uses <up_axis>Z_UP</up_axis>, and the COLLADA loader
applies (x_world, y_world, z_world) = (-x_dae, z_dae, y_dae).

Camera: world position (0, -0.4, 1.6), look direction (0, +0.55, -0.83) i.e.
upward and forward by ~30° pitch. Up vector world (0, 1, 0).

Light:  world position (-0.55, 1.49, -0.4), 0.4 x 0.4 area, brighter per-area
to compensate for the smaller emitter (keep total power similar).
"""
import math
from pathlib import Path

SRC = Path("dae/sky/CBspheres_lambertian.dae")
DST = Path("dae/sky/CBspheres_underwater.dae")
SURFACE_Y = 1.1

glass_cgl_extra = """      <extra>
        <technique profile="CGL">
          <glass>
            <reflectance>1 1 1</reflectance>
            <transmittance>1 1 1</transmittance>
            <roughness>0</roughness>
            <ior>1.33</ior>
          </glass>
        </technique>
      </extra>
"""

water_effect = """    <effect id="water-effect">
      <profile_COMMON>
        <technique sid="common">
          <phong>
            <diffuse><color sid="diffuse">0.8 0.9 1 1</color></diffuse>
          </phong>
        </technique>
      </profile_COMMON>
      <extra>
        <technique profile="CGL">
          <glass>
            <reflectance>1 1 1</reflectance>
            <transmittance>1 1 1</transmittance>
            <roughness>0</roughness>
            <ior>1.33</ior>
          </glass>
        </technique>
      </extra>
    </effect>
"""

water_material = """    <material id="water-material" name="water">
      <instance_effect url="#water-effect"/>
    </material>
"""

# Quad at DAE-z = SURFACE_Y, which maps to world-y = SURFACE_Y under the
# loader's Z_UP conversion. The DAE normal (0,0,1) maps to world up.
water_geometry = f"""    <geometry id="water-mesh" name="water">
      <mesh>
        <source id="water-mesh-positions">
          <float_array id="water-mesh-positions-array" count="12">1 -1 {SURFACE_Y} -1 -1 {SURFACE_Y} -1 1 {SURFACE_Y} 1 1 {SURFACE_Y}</float_array>
          <technique_common>
            <accessor source="#water-mesh-positions-array" count="4" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="water-mesh-normals">
          <float_array id="water-mesh-normals-array" count="6">0 0 1 0 0 1</float_array>
          <technique_common>
            <accessor source="#water-mesh-normals-array" count="2" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="water-mesh-map-0">
          <float_array id="water-mesh-map-0-array" count="12">0 1 0 1 0 1 0 1 0 1 0 1</float_array>
          <technique_common>
            <accessor source="#water-mesh-map-0-array" count="6" stride="2">
              <param name="S" type="float"/>
              <param name="T" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="water-mesh-vertices">
          <input semantic="POSITION" source="#water-mesh-positions"/>
        </vertices>
        <polylist material="water-material" count="2">
          <input semantic="VERTEX" source="#water-mesh-vertices" offset="0"/>
          <input semantic="NORMAL" source="#water-mesh-normals" offset="1"/>
          <input semantic="TEXCOORD" source="#water-mesh-map-0" offset="2" set="0"/>
          <vcount>3 3 </vcount>
          <p>0 0 0 1 0 1 2 0 2 0 1 3 2 1 4 3 1 5</p>
        </polylist>
      </mesh>
    </geometry>
"""

water_node = """      <node id="water" name="water" type="NODE">
        <instance_geometry url="#water-mesh">
          <bind_material>
            <technique_common>
              <instance_material symbol="water-material" target="#water-material"/>
            </technique_common>
          </bind_material>
        </instance_geometry>
      </node>
"""


def lookat_matrix_world(eye, target, up=(0.0, 1.0, 0.0)):
    """Build a column-major COLLADA-style local-to-world transform whose
    -Z column points from eye toward target. Returns a 4x4 row list."""
    ex, ey, ez = eye
    tx, ty, tz = target
    fx, fy, fz = tx - ex, ty - ey, tz - ez
    fl = math.sqrt(fx * fx + fy * fy + fz * fz)
    fx, fy, fz = fx / fl, fy / fl, fz / fl
    ux, uy, uz = up
    # right = forward x up (right-handed)
    rx = fy * uz - fz * uy
    ry = fz * ux - fx * uz
    rz = fx * uy - fy * ux
    rl = math.sqrt(rx * rx + ry * ry + rz * rz)
    rx, ry, rz = rx / rl, ry / rl, rz / rl
    # camera-up = right x forward (orthonormalized)
    cux = ry * fz - rz * fy
    cuy = rz * fx - rx * fz
    cuz = rx * fy - ry * fx
    # COLLADA camera looks down -Z, so the back column (col 2) is -forward.
    bx, by, bz = -fx, -fy, -fz
    return [
        rx, cux, bx, ex,
        ry, cuy, by, ey,
        rz, cuz, bz, ez,
        0.0, 0.0, 0.0, 1.0,
    ]


def world_to_dae_z_up(M_world):
    """Convert a 4x4 row-major world-space transform into the DAE Z_UP form
    that the loader will read. The loader applies T = R * M_dae where
    R = diag(-1, 0 0; 0 0 1; 0 1 0)  swaps Y<->Z and negates X. To produce
    a final world transform M_world, write M_dae = R^{-1} * M_world. R is
    its own inverse (involution).
    """
    R = [
        [-1, 0, 0, 0],
        [0, 0, 1, 0],
        [0, 1, 0, 0],
        [0, 0, 0, 1],
    ]
    Mw = [M_world[i:i + 4] for i in range(0, 16, 4)]
    out = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            out[i][j] = sum(R[i][k] * Mw[k][j] for k in range(4))
    return [v for row in out for v in row]


def fmt(v):
    return f"{v:.6g}"


# --- Camera pose -----------------------------------------------------------
eye = (0.0, -0.4, 1.6)
target = (0.0, 0.95, -0.5)  # upward and forward toward upper-back
cam_world = lookat_matrix_world(eye, target)
cam_dae = world_to_dae_z_up(cam_world)
cam_matrix_str = " ".join(fmt(v) for v in cam_dae)

# --- Light: smaller corner emitter ----------------------------------------
# In CBspheres_lambertian.dae the area emitter mesh is the same "ceiling"
# placeholder. The light *node* transform sets its world position. We move it
# toward the back-left corner of the ceiling and shrink its extent.
# Original transform (DAE):  0.6 0 0 0 / 0 0.8 0 0 / 0 0 1 1.49 / 0 0 0 1
#   -> world position (0, 1.49, 0), scale 0.6x0.8x1.0
# New: world position (-0.55, 1.49, -0.4), scale 0.3 x 0.3 x 1.0 (smaller).
# DAE-coordinate matrix derivation: world (-0.55, 1.49, -0.4) -> DAE position
#   x_dae = -x_world = 0.55
#   y_dae =  z_world = -0.4
#   z_dae =  y_world = 1.49
new_light_node = (
    "0.3 0 0 0.55 "
    "0 0.3 0 -0.4 "
    "0 0 1 1.49 "
    "0 0 0 1"
)

# Scale the emitter radiance up so the smaller emitter still lights the scene.
# Default radiance is "10 10 10" in CBspheres_lambertian.dae (per ceiling area).
# Original area = 0.6 * 0.8 = 0.48; new area = 0.3 * 0.3 = 0.09; ratio ~5.3x.
# Boost color from 10 to 30 to keep total emitted power roughly similar but
# make the source visibly small / sun-like.
src = SRC.read_text()
src = src.replace(
    '      </profile_COMMON>\n    </effect>\n    <effect id="ceiling-effect">',
    '      </profile_COMMON>\n' + glass_cgl_extra +
    '    </effect>\n    <effect id="ceiling-effect">',
    1,
)
src = src.replace("  </library_effects>", water_effect + "  </library_effects>", 1)
src = src.replace("  </library_materials>", water_material + "  </library_materials>", 1)
src = src.replace("  </library_geometries>", water_geometry + "  </library_geometries>", 1)
src = src.replace("    </visual_scene>", water_node + "    </visual_scene>", 1)

# Replace ONLY the Area light node's transform. We grep for the unique pattern
# preceding it. The light node is the one whose <instance_light> follows.
import re
m = re.search(
    r'(<node[^>]*id="Area"[^>]*>\s*<matrix[^>]*>)([^<]+)(</matrix>\s*<instance_light)',
    src, flags=re.S)
assert m, "could not locate Area light node transform"
src = src[:m.start(2)] + new_light_node + src[m.end(2):]

# Replace Camera node transform similarly.
m = re.search(
    r'(<node[^>]*id="Camera"[^>]*>\s*<matrix[^>]*>)([^<]+)(</matrix>\s*<instance_camera)',
    src, flags=re.S)
assert m, "could not locate Camera node transform"
src = src[:m.start(2)] + cam_matrix_str + src[m.end(2):]

# Boost the area-light radiance. The loader reads the CGL <area> extension's
# color (not the COLLADA <point> color, which it ignores). New emitter area
# is 0.09 vs original 0.48 (~5.3x smaller); push radiance to 80 so total
# emitted power ~1.5x the original — small bright source approximates a sun.
m = re.search(r'(<area>\s*<color[^>]*>)([^<]+)(</color>)', src, flags=re.S)
assert m, "could not locate area-light radiance"
src = src[:m.start(2)] + "80 80 80" + src[m.end(2):]

DST.write_text(src)
print(f"wrote {DST}")
