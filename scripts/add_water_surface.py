#!/usr/bin/env python3
"""Produce CBbunny_water.dae from CBbunny.dae by inserting a horizontal glass
slab (the air-water interface) at world y = SURFACE_Y.

Above the surface (world y > SURFACE_Y) = air (vacuum).
Below the surface (world y < SURFACE_Y) = water (chromatic participating
medium, configured in raytraced_renderer.cpp; BBox must have ymax = SURFACE_Y).

CBbunny.dae uses <up_axis>Z_UP</up_axis>, and the COLLADA loader applies
(x_world, y_world, z_world) = (-x_dae, z_dae, y_dae). To place a horizontal
slab at world y = SURFACE_Y we therefore write the slab into the DAE with
DAE z = SURFACE_Y (its DAE-up axis), spanning DAE x and y in [-1, 1], with
DAE normal (0, 0, 1) which maps to world (0, 1, 0).
"""

import sys
from pathlib import Path

SURFACE_Y = 1.1          # water-surface height
SRC = Path("dae/sky/CBbunny.dae")
DST = Path("dae/sky/CBbunny_water.dae")

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

# Quad at DAE-z = SURFACE_Y (which maps to world-y = SURFACE_Y under Z_UP),
# spanning DAE x,y in [-1, 1]. Two triangles. Normal (0,0,1) in DAE coords
# maps to (0,1,0) in world coords (i.e. pointing up out of the water).
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

src = SRC.read_text()
src = src.replace("  </library_effects>",    glass_effect   + "  </library_effects>", 1)
src = src.replace("  </library_materials>",  glass_material + "  </library_materials>", 1)
src = src.replace("  </library_geometries>", water_geometry + "  </library_geometries>", 1)
src = src.replace("    </visual_scene>",     water_node     + "    </visual_scene>", 1)

# Enlarge the ceiling-light mesh to the entire ceiling: every ray that
# refracts up through the water surface in any direction hits "sky".
# Original: 0.4 1.49 -0.3 ... (0.8 x 0.6 rectangle)
# New:      0.99 1.49 -0.99 ... (almost entire ceiling)
src = src.replace(
    '<float_array id="light-mesh-positions-array" count="12">0.4 1.49 -0.3 0.4 1.49 0.3 -0.4 1.49 0.3 -0.4 1.49 -0.3</float_array>',
    '<float_array id="light-mesh-positions-array" count="12">0.99 1.49 -0.99 0.99 1.49 0.99 -0.99 1.49 0.99 -0.99 1.49 -0.99</float_array>',
    1,
)
# Reduce per-area radiance so total power is similar to original:
# original area = 0.8 * 0.6 = 0.48  -> radiance 10
# new area     = 1.98 * 1.98 ≈ 3.92 -> radiance ≈ 10 * 0.48 / 3.92 ≈ 1.22
src = src.replace('<color sid="color">10 10 10</color>', '<color sid="color">4 4 4</color>')

DST.write_text(src)
print(f"wrote {DST}")
