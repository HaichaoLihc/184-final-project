#!/usr/bin/env python3
"""Build a VPM-focused underwater demo scene.

The base scene already has the underwater Cornell box, water surface, glass
sphere, diffuse sphere, and pebble field. This variant makes the light smaller
and brighter, then adds a white underwater reflector panel. Direct-volume
single scattering only sees camera-sample -> light paths; VPM can also use
photons that bounce from this reflector and then scatter in the medium.
"""

from pathlib import Path
import re


SRC = Path("dae/sky/CBspheres_underwater_pebbles.dae")
DST = Path("dae/sky/CBspheres_underwater_vpm_demo.dae")


def world_to_dae(p):
    x, y, z = p
    return (-x, z, y)


def fmt(v):
    return f"{v:.6g}"


def dae_points(points):
    return " ".join(fmt(c) for p in points for c in world_to_dae(p))


reflector_effect = """    <effect id="vpm-reflector-effect">
      <profile_COMMON>
        <technique sid="common">
          <phong>
            <diffuse><color sid="diffuse">0.95 0.92 0.82 1</color></diffuse>
          </phong>
        </technique>
      </profile_COMMON>
    </effect>
"""

reflector_material = """    <material id="vpm-reflector-material" name="vpm-reflector">
      <instance_effect url="#vpm-reflector-effect"/>
    </material>
"""

# A vertical reflector in the water, placed near the left/back side of the box.
# Keep it as a single two-triangle sheet. The half-edge conversion used by the
# viewer rejects coincident two-sided faces as non-manifold geometry.
p0 = (-0.92, -0.82, -0.78)
p1 = (-0.92, -0.82,  0.48)
p2 = (-0.92,  1.03,  0.48)
p3 = (-0.92,  1.03, -0.78)
positions = dae_points([p0, p1, p2, p3])

reflector_geometry = f"""    <geometry id="vpm-reflector-mesh" name="vpm-reflector">
      <mesh>
        <source id="vpm-reflector-mesh-positions">
          <float_array id="vpm-reflector-mesh-positions-array" count="12">{positions}</float_array>
          <technique_common>
            <accessor source="#vpm-reflector-mesh-positions-array" count="4" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="vpm-reflector-mesh-normals">
          <float_array id="vpm-reflector-mesh-normals-array" count="3">-1 0 0</float_array>
          <technique_common>
            <accessor source="#vpm-reflector-mesh-normals-array" count="1" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="vpm-reflector-mesh-vertices">
          <input semantic="POSITION" source="#vpm-reflector-mesh-positions"/>
        </vertices>
        <polylist material="vpm-reflector-material" count="2">
          <input semantic="VERTEX" source="#vpm-reflector-mesh-vertices" offset="0"/>
          <input semantic="NORMAL" source="#vpm-reflector-mesh-normals" offset="1"/>
          <vcount>3 3</vcount>
          <p>0 0 1 0 2 0 0 0 2 0 3 0</p>
        </polylist>
      </mesh>
    </geometry>
"""

reflector_node = """      <node id="vpm-reflector" name="vpm-reflector" type="NODE">
        <instance_geometry url="#vpm-reflector-mesh">
          <bind_material>
            <technique_common>
              <instance_material symbol="vpm-reflector-material" target="#vpm-reflector-material"/>
            </technique_common>
          </bind_material>
        </instance_geometry>
      </node>
"""

# A near-surface bounce card under the water. It blocks many direct
# scatter-point-to-light shadow rays, but photon paths can hit this card,
# bounce downward, and then scatter in the medium. This is the main VPM stress
# case: indirect volume lighting after a diffuse surface bounce.
c0 = (-0.90, 0.98, -0.85)
c1 = ( 0.25, 0.98, -0.85)
c2 = ( 0.25, 0.98,  0.25)
c3 = (-0.90, 0.98,  0.25)
canopy_positions = dae_points([c0, c1, c2, c3])

canopy_geometry = f"""    <geometry id="vpm-bounce-canopy-mesh" name="vpm-bounce-canopy">
      <mesh>
        <source id="vpm-bounce-canopy-mesh-positions">
          <float_array id="vpm-bounce-canopy-mesh-positions-array" count="12">{canopy_positions}</float_array>
          <technique_common>
            <accessor source="#vpm-bounce-canopy-mesh-positions-array" count="4" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <source id="vpm-bounce-canopy-mesh-normals">
          <float_array id="vpm-bounce-canopy-mesh-normals-array" count="3">0 0 -1</float_array>
          <technique_common>
            <accessor source="#vpm-bounce-canopy-mesh-normals-array" count="1" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
        <vertices id="vpm-bounce-canopy-mesh-vertices">
          <input semantic="POSITION" source="#vpm-bounce-canopy-mesh-positions"/>
        </vertices>
        <polylist material="vpm-reflector-material" count="2">
          <input semantic="VERTEX" source="#vpm-bounce-canopy-mesh-vertices" offset="0"/>
          <input semantic="NORMAL" source="#vpm-bounce-canopy-mesh-normals" offset="1"/>
          <vcount>3 3</vcount>
          <p>0 0 1 0 2 0 0 0 2 0 3 0</p>
        </polylist>
      </mesh>
    </geometry>
"""

canopy_node = """      <node id="vpm-bounce-canopy" name="vpm-bounce-canopy" type="NODE">
        <instance_geometry url="#vpm-bounce-canopy-mesh">
          <bind_material>
            <technique_common>
              <instance_material symbol="vpm-reflector-material" target="#vpm-reflector-material"/>
            </technique_common>
          </bind_material>
        </instance_geometry>
      </node>
"""


src = SRC.read_text()

# Use a smaller, brighter top-left area light. This creates high-contrast
# incident photon paths and makes water-surface perturbations more visible.
new_light_node = (
    "0.16 0 0 0.70 "
    "0 0.16 0 -0.58 "
    "0 0 1 1.49 "
    "0 0 0 1"
)
m = re.search(
    r'(<node[^>]*id="Area"[^>]*>\s*<matrix[^>]*>)([^<]+)(</matrix>\s*<instance_light)',
    src,
    flags=re.S,
)
assert m, "could not locate Area light node transform"
src = src[:m.start(2)] + new_light_node + src[m.end(2):]

m = re.search(r'(<area>\s*<color[^>]*>)([^<]+)(</color>)', src, flags=re.S)
assert m, "could not locate area-light radiance"
src = src[:m.start(2)] + "220 210 170" + src[m.end(2):]

src = src.replace("  </library_effects>", reflector_effect + "  </library_effects>", 1)
src = src.replace("  </library_materials>", reflector_material + "  </library_materials>", 1)
src = src.replace(
    "  </library_geometries>",
    reflector_geometry + canopy_geometry + "  </library_geometries>",
    1,
)
src = src.replace(
    "    </visual_scene>",
    reflector_node + canopy_node + "    </visual_scene>",
    1,
)

DST.write_text(src)
print(f"wrote {DST}")
