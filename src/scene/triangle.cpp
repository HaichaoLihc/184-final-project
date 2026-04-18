#include "triangle.h"

#include "CGL/CGL.h"
#include "GL/glew.h"

namespace CGL {
namespace SceneObjects {

Triangle::Triangle(const Mesh *mesh, size_t v1, size_t v2, size_t v3) {
  p1 = mesh->positions[v1];
  p2 = mesh->positions[v2];
  p3 = mesh->positions[v3];
  n1 = mesh->normals[v1];
  n2 = mesh->normals[v2];
  n3 = mesh->normals[v3];
  bbox = BBox(p1);
  bbox.expand(p2);
  bbox.expand(p3);

  bsdf = mesh->get_bsdf();
}

BBox Triangle::get_bbox() const { return bbox; }

bool Triangle::has_intersection(const Ray &r) const {
  // Part 1, Task 3: implement ray-triangle intersection
  // The difference between this function and the next function is that the next
  // function records the "intersection" while this function only tests whether
  // there is a intersection.
  const Vector3D e1 = p2 - p1;
  const Vector3D e2 = p3 - p1;
  const Vector3D pvec = cross(r.d, e2);
  const double det = dot(e1, pvec);

  if (fabs(det) < EPS_F) {
    return false;
  }

  const double inv_det = 1.0 / det;
  const Vector3D tvec = r.o - p1;
  const double u = dot(tvec, pvec) * inv_det;

  if (u < 0.0 || u > 1.0) {
    return false;
  }

  const Vector3D qvec = cross(tvec, e1);
  const double v = dot(r.d, qvec) * inv_det;

  if (v < 0.0 || u + v > 1.0) {
    return false;
  }

  const double t = dot(e2, qvec) * inv_det;
  if (t < r.min_t || t > r.max_t) {
    return false;
  }

  r.max_t = t;
  return true;

}

bool Triangle::intersect(const Ray &r, Intersection *isect) const {
  // Part 1, Task 3:
  // implement ray-triangle intersection. When an intersection takes
  // place, the Intersection data should be updated accordingly
  const Vector3D e1 = p2 - p1;
  const Vector3D e2 = p3 - p1;
  const Vector3D pvec = cross(r.d, e2);
  const double det = dot(e1, pvec);

  if (fabs(det) < EPS_F) {
    return false;
  }

  const double inv_det = 1.0 / det;
  const Vector3D tvec = r.o - p1;
  const double u = dot(tvec, pvec) * inv_det;

  if (u < 0.0 || u > 1.0) {
    return false;
  }

  const Vector3D qvec = cross(tvec, e1);
  const double v = dot(r.d, qvec) * inv_det;

  if (v < 0.0 || u + v > 1.0) {
    return false;
  }

  const double t = dot(e2, qvec) * inv_det;
  if (t < r.min_t || t > r.max_t) {
    return false;
  }

  const double w = 1.0 - u - v;
  r.max_t = t;
  isect->t = t;
  isect->n = (w * n1 + u * n2 + v * n3).unit();
  isect->primitive = this;
  isect->bsdf = get_bsdf();
  return true;


}

void Triangle::draw(const Color &c, float alpha) const {
  glColor4f(c.r, c.g, c.b, alpha);
  glBegin(GL_TRIANGLES);
  glVertex3d(p1.x, p1.y, p1.z);
  glVertex3d(p2.x, p2.y, p2.z);
  glVertex3d(p3.x, p3.y, p3.z);
  glEnd();
}

void Triangle::drawOutline(const Color &c, float alpha) const {
  glColor4f(c.r, c.g, c.b, alpha);
  glBegin(GL_LINE_LOOP);
  glVertex3d(p1.x, p1.y, p1.z);
  glVertex3d(p2.x, p2.y, p2.z);
  glVertex3d(p3.x, p3.y, p3.z);
  glEnd();
}

} // namespace SceneObjects
} // namespace CGL
