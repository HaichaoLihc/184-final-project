#!/usr/bin/env python3
"""Produce CBbunny_water.dae from CBbunny.dae by inserting a wavy glass slab
(the air-water interface) at world y = SURFACE_Y.

The slab is an N×N subdivided grid spanning world x,z in [-1, 1], with each
vertex displaced vertically by a sum-of-sinusoids height field h(x, z) and
each vertex normal computed analytically from the gradient of h. This gives
proper geometric refraction (caustic focusing, ripple distortion) when the
glass BSDF samples Snell's law against the per-vertex shading normal.

Above the surface (world y > SURFACE_Y_MAX_disp) ≈ air (vacuum).
Below the surface (world y < SURFACE_Y_MIN_disp) = water (chromatic
participating medium configured in raytraced_renderer.cpp).

CBbunny.dae is <up_axis>Z_UP</up_axis>, and the COLLADA loader applies
(x_world, y_world, z_world) = (-x_dae, z_dae, y_dae). To put a horizontal
slab at world y = SURFACE_Y we therefore put the height on DAE-Z and the
in-plane span on DAE-X / DAE-Y. World normal (0, 1, 0) corresponds to
DAE normal (0, 0, 1), and likewise dh/dx_world / dh/dz_world map back to
DAE-x / DAE-y derivatives respectively (with a sign flip on x since the
loader negates X).
"""

import math
from pathlib import Path

SURFACE_Y = 1.1          # mean water-surface height (world)
SRC = Path("dae/sky/CBbunny.dae")
DST = Path("dae/sky/CBbunny_water.dae")

# --- Wavy mesh parameters ---
N = 64                   # grid resolution: (N+1)^2 vertices, 2 N^2 triangles
HALF_EXTENT = 1.0        # span from -HALF_EXTENT to +HALF_EXTENT in world x and z

# Two sinusoids combined for a non-axis-aligned ripple pattern. Amplitude
# kept small so the slab stays clearly horizontal (refraction stable).
WAVE_AMP   = 0.045
WAVE_K1    = 7.0
WAVE_K2    = 11.0
WAVE_PHASE_X = 0.31
WAVE_PHASE_Z = 1.27
WAVE_PHASE_D = 2.11


def height(x_world, z_world):
    """h(x, z) returning world-y displacement above SURFACE_Y."""
    return WAVE_AMP * (
        math.sin(WAVE_K1 * x_world + WAVE_PHASE_X)
        * math.cos(WAVE_K1 * z_world + WAVE_PHASE_Z)
        + 0.5 * math.sin(WAVE_K2 * (x_world + z_world) + WAVE_PHASE_D)
    )


def gradient(x_world, z_world):
    """∂h/∂x_world, ∂h/∂z_world (analytic)."""
    dhdx = WAVE_AMP * (
        WAVE_K1 * math.cos(WAVE_K1 * x_world + WAVE_PHASE_X)
        * math.cos(WAVE_K1 * z_world + WAVE_PHASE_Z)
        + 0.5 * WAVE_K2 * math.cos(WAVE_K2 * (x_world + z_world) + WAVE_PHASE_D)
    )
    dhdz = WAVE_AMP * (
        -WAVE_K1 * math.sin(WAVE_K1 * x_world + WAVE_PHASE_X)
        * math.sin(WAVE_K1 * z_world + WAVE_PHASE_Z)
        + 0.5 * WAVE_K2 * math.cos(WAVE_K2 * (x_world + z_world) + WAVE_PHASE_D)
    )
    return dhdx, dhdz


def world_normal(x_world, z_world):
    """Analytic surface normal in world coordinates from h(x, z)."""
    dhdx, dhdz = gradient(x_world, z_world)
    nx, ny, nz = -dhdx, 1.0, -dhdz
    inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
    return nx * inv, ny * inv, nz * inv


def world_to_dae(p_world):
    """Map world (x,y,z) -> DAE (x,y,z) under <up_axis>Z_UP</up_axis>:
    x_dae = -x_world, y_dae = z_world, z_dae = y_world."""
    xw, yw, zw = p_world
    return (-xw, zw, yw)


# --- Build vertex / normal / triangle index arrays ---
positions = []  # flat list of DAE x,y,z floats
normals   = []  # flat list of DAE x,y,z floats
indices   = []  # flat list per triangle vertex of (vert_idx, norm_idx, tex_idx)

for i in range(N + 1):
    for j in range(N + 1):
        u = i / N
        v = j / N
        xw = -HALF_EXTENT + 2.0 * HALF_EXTENT * u
        zw = -HALF_EXTENT + 2.0 * HALF_EXTENT * v
        yw = SURFACE_Y + height(xw, zw)
        xd, yd, zd = world_to_dae((xw, yw, zw))
        positions.extend([xd, yd, zd])
        nxw, nyw, nzw = world_normal(xw, zw)
        # Normals transform the same way as positions under the loader's
        # rigid Z_UP rotation: x_dae = -x_world, y_dae = z_world, z_dae = y_world.
        nxd, nyd, nzd = -nxw, nzw, nyw
        normals.extend([nxd, nyd, nzd])

# Two triangles per cell, sharing vertex indices == normal indices.
def vid(i, j):
    return i * (N + 1) + j

for i in range(N):
    for j in range(N):
        v00 = vid(i,     j)
        v10 = vid(i + 1, j)
        v11 = vid(i + 1, j + 1)
        v01 = vid(i,     j + 1)
        # Counter-clockwise so the normal ends up world +Y.
        # Triangle 1: v00, v10, v11
        # Triangle 2: v00, v11, v01
        for vi in (v00, v10, v11, v00, v11, v01):
            indices.append((vi, vi, 0))  # tex index always 0 (single dummy uv)

num_verts  = (N + 1) * (N + 1)
num_norms  = num_verts
num_tris   = 2 * N * N
num_floats_pos = 3 * num_verts
num_floats_nrm = 3 * num_norms

positions_str = " ".join(f"{v:.6f}" for v in positions)
normals_str   = " ".join(f"{v:.6f}" for v in normals)
indices_str   = " ".join(f"{vi} {ni} {ti}" for vi, ni, ti in indices)

# --- DAE building blocks ---
glass_effect = """    <effect id="water-effect">
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

glass_material = """    <material id="water-material" name="water">
      <instance_effect url="#water-effect"/>
    </material>
"""

water_geometry = f"""    <geometry id="water-mesh" name="water">
      <mesh>
        <source id="water-mesh-positions">
          <float_array id="water-mesh-positions-array" count="{num_floats_pos}">{positions_str}</float_array>
          <technique_common>
            <accessor source="#water-mesh-positions-array" count="{num_verts}" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="water-mesh-normals">
          <float_array id="water-mesh-normals-array" count="{num_floats_nrm}">{normals_str}</float_array>
          <technique_common>
            <accessor source="#water-mesh-normals-array" count="{num_norms}" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="water-mesh-map-0">
          <float_array id="water-mesh-map-0-array" count="2">0 0</float_array>
          <technique_common>
            <accessor source="#water-mesh-map-0-array" count="1" stride="2">
              <param name="S" type="float"/>
              <param name="T" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="water-mesh-vertices">
          <input semantic="POSITION" source="#water-mesh-positions"/>
        </vertices>
        <polylist material="water-material" count="{num_tris}">
          <input semantic="VERTEX" source="#water-mesh-vertices" offset="0"/>
          <input semantic="NORMAL" source="#water-mesh-normals" offset="1"/>
          <input semantic="TEXCOORD" source="#water-mesh-map-0" offset="2" set="0"/>
          <vcount>{' '.join(['3'] * num_tris)}</vcount>
          <p>{indices_str}</p>
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

src = SRC.read_text()
src = src.replace("  </library_effects>",    glass_effect   + "  </library_effects>", 1)
src = src.replace("  </library_materials>",  glass_material + "  </library_materials>", 1)
src = src.replace("  </library_geometries>", water_geometry + "  </library_geometries>", 1)
src = src.replace("    </visual_scene>",     water_node     + "    </visual_scene>", 1)

# Enlarge the ceiling-light mesh to most of the ceiling — every ray that
# refracts upward through the water in any direction should be able to
# hit "sky".
src = src.replace(
    '<float_array id="light-mesh-positions-array" count="12">0.4 1.49 -0.3 0.4 1.49 0.3 -0.4 1.49 0.3 -0.4 1.49 -0.3</float_array>',
    '<float_array id="light-mesh-positions-array" count="12">0.99 1.49 -0.99 0.99 1.49 0.99 -0.99 1.49 0.99 -0.99 1.49 -0.99</float_array>',
    1,
)
# Reduce per-area radiance so total power matches the original tiny emitter:
# original area 0.8 * 0.6 = 0.48 with radiance 10 -> ~1.22 over the new
# 1.98 * 1.98 ≈ 3.92 area. Round up to 4 to stay bright underwater.
src = src.replace('<color sid="color">10 10 10</color>',
                  '<color sid="color">4 4 4</color>')

DST.write_text(src)
print(f"wrote {DST}: {num_verts} verts, {num_tris} tris (N={N}, amp={WAVE_AMP})")
