#!/usr/bin/env python3
"""Build an underwater COLLADA scene around the OBJ in 3d_models.

The installed Blender build on this machine currently crashes in background
mode, so this script writes the small subset of COLLADA that the CS184 parser
supports: Y_UP coordinates, phong diffuse materials, a CGL glass water surface,
and triangle/polylist geometry.
"""

import argparse
import math
from pathlib import Path
from xml.sax.saxutils import escape


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OBJ = ROOT / "3d_models" / "09fefb538525b93db725f85c03aa8044.obj"
DEFAULT_DAE = ROOT / "dae" / "underwater" / "obj_underwater.dae"
DEFAULT_CAM = ROOT / "dae" / "underwater" / "obj_underwater_cam.txt"

SURFACE_Y = 1.10


def parse_index_value(idx, count):
    if not idx:
        return None
    value = int(idx)
    if value < 0:
        value = count + value + 1
    return value - 1


def read_obj_sample(path, target_faces):
    vertices = []
    normals = []
    face_count = 0
    with path.open() as f:
        for line in f:
            if line.startswith("v "):
                _, x, y, z, *_ = line.split()
                vertices.append((float(x), float(y), float(z)))
            elif line.startswith("vn "):
                _, x, y, z, *_ = line.split()
                normals.append((float(x), float(y), float(z)))
            elif line.startswith("f "):
                parts = line.split()[1:]
                face_count += max(0, len(parts) - 2)

    if not vertices or not face_count:
        raise RuntimeError(f"{path} did not contain usable vertices/faces")

    stride = max(1, math.ceil(face_count / target_faces))
    sampled = []
    tri_index = 0
    with path.open() as f:
        for line in f:
            if not line.startswith("f "):
                continue
            parts = line.split()[1:]
            if len(parts) < 3:
                continue
            face_indices = []
            for p in parts:
                fields = p.split("/")
                vi = parse_index_value(fields[0], len(vertices))
                ni = parse_index_value(fields[2], len(normals)) if len(fields) > 2 else None
                face_indices.append((vi, ni))
            for i in range(1, len(face_indices) - 1):
                if tri_index % stride == 0:
                    sampled.append((face_indices[0], face_indices[i], face_indices[i + 1]))
                tri_index += 1

    used = {}
    used_normals = {}
    compact_vertices = []
    compact_normals = []
    compact_faces = []
    for face in sampled:
        compact_vertices_face = []
        compact_normals_face = []
        for vi, ni in face:
            mapped = used.get(vi)
            if mapped is None:
                mapped = len(compact_vertices)
                used[vi] = mapped
                compact_vertices.append(vertices[vi])
            compact_vertices_face.append(mapped)
            if ni is not None:
                mapped_normal = used_normals.get(ni)
                if mapped_normal is None:
                    mapped_normal = len(compact_normals)
                    used_normals[ni] = mapped_normal
                    compact_normals.append(normals[ni])
                compact_normals_face.append(mapped_normal)
            else:
                compact_normals_face.append(None)
        if len(set(compact_vertices_face)) == 3:
            compact_faces.append(tuple(zip(compact_vertices_face, compact_normals_face)))

    return compact_vertices, compact_normals, compact_faces, face_count, stride


def fit_vertices(vertices):
    min_x = min(v[0] for v in vertices)
    max_x = max(v[0] for v in vertices)
    min_y = min(v[1] for v in vertices)
    max_y = max(v[1] for v in vertices)
    min_z = min(v[2] for v in vertices)
    max_z = max(v[2] for v in vertices)

    center_x = 0.5 * (min_x + max_x)
    center_z = 0.5 * (min_z + max_z)
    scale = min(
        1.20 / max(1e-9, max_x - min_x),
        0.82 / max(1e-9, max_y - min_y),
        1.20 / max(1e-9, max_z - min_z),
    )

    out = []
    for x, y, z in vertices:
        out.append(((x - center_x) * scale,
                    (y - min_y) * scale + 0.04,
                    (z - center_z) * scale))
    return out


def fmt_float(v):
    return f"{v:.6g}"


def floats(values):
    return " ".join(fmt_float(v) for v in values)


def ints(values):
    return " ".join(str(v) for v in values)


def effect_xml(effect_id, diffuse):
    r, g, b = diffuse
    return f"""    <effect id="{effect_id}">
      <profile_COMMON>
        <technique sid="common">
          <phong>
            <emission><color sid="emission">0 0 0 1</color></emission>
            <ambient><color sid="ambient">0 0 0 1</color></ambient>
            <diffuse><color sid="diffuse">{r} {g} {b} 1</color></diffuse>
            <specular><color sid="specular">0 0 0 1</color></specular>
            <shininess><float sid="shininess">1</float></shininess>
            <index_of_refraction><float sid="index_of_refraction">1</float></index_of_refraction>
          </phong>
        </technique>
      </profile_COMMON>
    </effect>
"""


def glass_effect_xml():
    return """    <effect id="water-effect">
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


def emission_effect_xml():
    return """    <effect id="sun-panel-effect">
      <profile_COMMON>
        <technique sid="common">
          <phong>
            <diffuse><color sid="diffuse">1 0.96 0.75 1</color></diffuse>
          </phong>
        </technique>
      </profile_COMMON>
      <extra>
        <technique profile="CGL">
          <emission><radiance>12 11 8</radiance></emission>
        </technique>
      </extra>
    </effect>
"""


def material_xml(material_id, effect_id, name=None):
    return f"""    <material id="{material_id}" name="{name or material_id}">
      <instance_effect url="#{effect_id}"/>
    </material>
"""


def geometry_xml(geom_id, vertices, faces, material_id, normals=None):
    pos = []
    for v in vertices:
        pos.extend(v)
    vcount = [3] * len(faces)
    indices = []
    has_normals = bool(normals) and all(all(ni is not None for _, ni in face) for face in faces)
    for face in faces:
        for vi, ni in face:
            indices.append(vi)
            if has_normals:
                indices.append(ni)
    normal_source = ""
    normal_input = ""
    if has_normals:
        normal_values = []
        for n in normals:
            normal_values.extend(n)
        normal_source = f"""        <source id="{geom_id}-normals">
          <float_array id="{geom_id}-normals-array" count="{len(normal_values)}">{floats(normal_values)}</float_array>
          <technique_common>
            <accessor source="#{geom_id}-normals-array" count="{len(normals)}" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
"""
        normal_input = f"""          <input semantic="NORMAL" source="#{geom_id}-normals" offset="1"/>
"""
    return f"""    <geometry id="{geom_id}" name="{geom_id}">
      <mesh>
        <source id="{geom_id}-positions">
          <float_array id="{geom_id}-positions-array" count="{len(pos)}">{floats(pos)}</float_array>
          <technique_common>
            <accessor source="#{geom_id}-positions-array" count="{len(vertices)}" stride="3">
              <param name="X" type="float"/>
              <param name="Y" type="float"/>
              <param name="Z" type="float"/>
            </accessor>
          </technique_common>
        </source>
{normal_source}        <vertices id="{geom_id}-vertices">
          <input semantic="POSITION" source="#{geom_id}-positions"/>
        </vertices>
        <polylist material="{material_id}" count="{len(faces)}">
          <input semantic="VERTEX" source="#{geom_id}-vertices" offset="0"/>
{normal_input}          <vcount>{ints(vcount)}</vcount>
          <p>{ints(indices)}</p>
        </polylist>
      </mesh>
    </geometry>
"""


def node_xml(node_id, geom_id, material_id):
    return f"""      <node id="{node_id}" name="{node_id}" type="NODE">
        <instance_geometry url="#{geom_id}">
          <bind_material>
            <technique_common>
              <instance_material symbol="{material_id}" target="#{material_id}"/>
            </technique_common>
          </bind_material>
        </instance_geometry>
      </node>
"""


def room_meshes(include_water=True):
    meshes = {
        "floor": (
            [(-1, 0, -1), (-1, 0, 1), (1, 0, 1), (1, 0, -1)],
            [(0, 1, 2), (0, 2, 3)],
            "sand-material",
        ),
        "back-wall": (
            [(-1, 0, -1), (1, 0, -1), (1, 1.50, -1), (-1, 1.50, -1)],
            [(0, 1, 2), (0, 2, 3)],
            "wall-material",
        ),
        "left-wall": (
            [(-1, 0, 1), (-1, 0, -1), (-1, 1.50, -1), (-1, 1.50, 1)],
            [(0, 1, 2), (0, 2, 3)],
            "wall-material",
        ),
        "right-wall": (
            [(1, 0, -1), (1, 0, 1), (1, 1.50, 1), (1, 1.50, -1)],
            [(0, 1, 2), (0, 2, 3)],
            "wall-material",
        ),
        "ceiling": (
            [(-1, 1.50, 1), (-1, 1.50, -1), (1, 1.50, -1), (1, 1.50, 1)],
            [(0, 1, 2), (0, 2, 3)],
            "ceiling-material",
        ),
        "water": (
            [(1, SURFACE_Y, -1), (-1, SURFACE_Y, -1), (-1, SURFACE_Y, 1), (1, SURFACE_Y, 1)],
            [(0, 1, 2), (0, 2, 3)],
            "water-material",
        ),
        "sun-panel": (
            [(-0.70, 1.48, -0.70), (-0.35, 1.48, -0.70), (-0.35, 1.48, -0.35), (-0.70, 1.48, -0.35)],
            [(0, 2, 1), (0, 3, 2)],
            "sun-panel-material",
        ),
    }
    if not include_water:
        meshes.pop("water", None)
    return meshes


def lookat_camera_settings(path, eye, target, screen_w=480, screen_h=360, hfov_deg=55.0):
    ex, ey, ez = eye
    tx, ty, tz = target
    back = (ex - tx, ey - ty, ez - tz)
    r = math.sqrt(sum(v * v for v in back))
    sz = tuple(v / r for v in back)
    up = (0.0, 1.0, 0.0)

    def cross(a, b):
        return (a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0])

    def norm(v):
        n = math.sqrt(sum(c * c for c in v))
        return tuple(c / n for c in v)

    sx = norm(cross(up, back))
    sy = norm(cross(back, sx))
    hfov = math.radians(hfov_deg)
    screen_dist = screen_w / (2.0 * math.tan(hfov / 2.0))
    vfov = math.degrees(2.0 * math.atan(screen_h / (2.0 * screen_dist)))
    ar = screen_w / screen_h
    phi = math.acos(max(-1.0, min(1.0, sz[1])))
    theta = math.atan2(sz[0], sz[2])

    rows = [
        f"{hfov_deg:.6f} {vfov:.6f} {ar:.6f} 0.001000 1000.000000",
        f"{ex:.6f} {ey:.6f} {ez:.6f} {tx:.6f} {ty:.6f} {tz:.6f}",
        f"{phi:.6f} {theta:.6f} {r:.6f} {r * 0.1:.6f} {r * 10.0:.6f}",
        " ".join(f"{v:.6f}" for v in [
            sx[0], sy[0], sz[0],
            sx[1], sy[1], sz[1],
            sx[2], sy[2], sz[2],
        ]),
        f"{screen_w} {screen_h} {screen_dist:.6f}",
        f"{r:.6f} 0.0",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(rows) + "\n")


def write_scene(obj_path, dae_path, cam_path, target_faces, include_water=True):
    raw_vertices, raw_normals, faces, source_face_count, stride = read_obj_sample(obj_path, target_faces)
    fitted_vertices = fit_vertices(raw_vertices)
    if stride == 1:
        # Preserve the original connectivity for full-density export. This lets
        # the halfedge mesh compute smooth-ish vertex normals instead of forcing
        # every triangle to shade independently.
        obj_vertices = fitted_vertices
        obj_normals = raw_normals
        obj_faces = faces
    else:
        # A decimated-by-sampling OBJ can create vertices with disconnected face
        # fans, which the halfedge builder rejects. For previews, emit the model
        # as disconnected triangles.
        obj_vertices = []
        obj_normals = []
        obj_faces = []
        for face in faces:
            base = len(obj_vertices)
            normal_base = len(obj_normals)
            new_face = []
            for vi, ni in face:
                obj_vertices.append(fitted_vertices[vi])
                if ni is not None:
                    obj_normals.append(raw_normals[ni])
                    new_face.append((base + len(new_face), normal_base + len(new_face)))
                else:
                    new_face.append((base + len(new_face), None))
            obj_faces.append(tuple(new_face))
    dae_path.parent.mkdir(parents=True, exist_ok=True)

    effects = [
        effect_xml("obj-effect", (0.74, 0.78, 0.70)),
        effect_xml("sand-effect", (0.66, 0.62, 0.48)),
        effect_xml("wall-effect", (0.28, 0.38, 0.42)),
        effect_xml("ceiling-effect", (0.50, 0.64, 0.70)),
        glass_effect_xml(),
        emission_effect_xml(),
    ]
    materials = [
        material_xml("obj-material", "obj-effect", "submerged-obj"),
        material_xml("sand-material", "sand-effect", "sand"),
        material_xml("wall-material", "wall-effect", "wall"),
        material_xml("ceiling-material", "ceiling-effect", "ceiling"),
        material_xml("water-material", "water-effect", "water"),
        material_xml("sun-panel-material", "sun-panel-effect", "sun-panel"),
    ]

    geometries = [geometry_xml("obj-mesh", obj_vertices, obj_faces, "obj-material", obj_normals)]
    nodes = [node_xml("obj-model", "obj-mesh", "obj-material")]
    for name, (verts, tris, mat) in room_meshes(include_water).items():
        geom_id = f"{name}-mesh"
        room_faces = [tuple((idx, None) for idx in tri) for tri in tris]
        geometries.append(geometry_xml(geom_id, verts, room_faces, mat))
        nodes.append(node_xml(name, geom_id, mat))

    # Area light transform: position at (-0.525, 1.48, -0.525), direction down,
    # dimensions 0.35 x 0.35 in x/z. The renderer's area light uses the transform columns
    # to derive direction and dimensions from the default local light frame.
    area_light_transform = "-0.35 0 0 -0.525 0 0 1 1.48 0 0.35 0 -0.525 0 0 0 1"

    dae = f"""<?xml version="1.0" encoding="utf-8"?>
<COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
  <asset>
    <unit name="meter" meter="1"/>
    <up_axis>Y_UP</up_axis>
  </asset>
  <library_lights>
    <light id="sun-area-light" name="SunArea">
      <technique_common>
        <point>
          <color sid="color">3 3 2.4</color>
          <constant_attenuation>1</constant_attenuation>
          <linear_attenuation>0</linear_attenuation>
          <quadratic_attenuation>0</quadratic_attenuation>
        </point>
      </technique_common>
      <extra>
        <technique profile="CGL">
          <area><color sid="color">46 45 36</color></area>
        </technique>
      </extra>
    </light>
    <light id="fill-light" name="Fill">
      <technique_common>
        <point>
          <color sid="color">0.95 1.08 1.00</color>
          <constant_attenuation>1</constant_attenuation>
          <linear_attenuation>0</linear_attenuation>
          <quadratic_attenuation>0</quadratic_attenuation>
        </point>
      </technique_common>
    </light>
  </library_lights>
  <library_cameras>
    <camera id="Camera-camera" name="Camera">
      <optics>
        <technique_common>
          <perspective>
            <xfov sid="xfov">55</xfov>
            <aspect_ratio>1.333333</aspect_ratio>
            <znear sid="znear">0.001</znear>
            <zfar sid="zfar">1000</zfar>
          </perspective>
        </technique_common>
      </optics>
    </camera>
  </library_cameras>
  <library_effects>
{''.join(effects)}  </library_effects>
  <library_materials>
{''.join(materials)}  </library_materials>
  <library_geometries>
{''.join(geometries)}  </library_geometries>
  <library_visual_scenes>
    <visual_scene id="Scene" name="Scene">
      <node id="SunArea" name="SunArea" type="NODE">
        <matrix sid="transform">{area_light_transform}</matrix>
        <instance_light url="#sun-area-light"/>
      </node>
      <node id="Fill" name="Fill" type="NODE">
        <matrix sid="transform">1 0 0 0 0 1 0 0.62 0 0 1 0.78 0 0 0 1</matrix>
        <instance_light url="#fill-light"/>
      </node>
      <node id="Camera" name="Camera" type="NODE">
        <matrix sid="transform">1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1</matrix>
        <instance_camera url="#Camera-camera"/>
      </node>
{''.join(nodes)}    </visual_scene>
  </library_visual_scenes>
  <scene>
    <instance_visual_scene url="#Scene"/>
  </scene>
</COLLADA>
"""
    dae_path.write_text(dae)
    lookat_camera_settings(cam_path, eye=(0.0, 0.52, 0.95), target=(0.0, 0.43, -0.03))
    print(f"source faces: {source_face_count}")
    print(f"sample stride: {stride}")
    print(f"written faces: {len(obj_faces)}")
    print(f"written vertices: {len(obj_vertices)}")
    print(f"wrote {dae_path}")
    print(f"wrote {cam_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--obj", type=Path, default=DEFAULT_OBJ)
    parser.add_argument("--out", type=Path, default=DEFAULT_DAE)
    parser.add_argument("--camera", type=Path, default=DEFAULT_CAM)
    parser.add_argument("--target-faces", type=int, default=60000)
    parser.add_argument("--no-water", action="store_true",
                        help="omit the glass water surface geometry")
    args = parser.parse_args()

    write_scene(args.obj, args.out, args.camera, args.target_faces,
                include_water=not args.no_water)


if __name__ == "__main__":
    main()
