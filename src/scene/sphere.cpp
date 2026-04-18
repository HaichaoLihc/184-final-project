#include "sphere.h"

#include <cmath>

#include "pathtracer/bsdf.h"
#include "util/sphere_drawing.h"

namespace CGL {
namespace SceneObjects {

bool Sphere::test(const Ray &r, double &t1, double &t2) const {

  // TODO (Part 1.4):
  // Implement ray - sphere intersection test.
  // Return true if there are intersections and writing the
  // smaller of the two intersection times in t1 and the larger in t2.
  const Vector3D oc = r.o - o;
  const double a = dot(r.d, r.d);
  const double b = 2.0 * dot(oc, r.d);
  const double c = dot(oc, oc) - r2;
  const double discriminant = b * b - 4.0 * a * c;

  if (discriminant < 0.0) {
    return false;
  }

  const double sqrt_discriminant = sqrt(discriminant);
  t1 = (-b - sqrt_discriminant) / (2.0 * a);
  t2 = (-b + sqrt_discriminant) / (2.0 * a);

  if (t1 > t2) {
    std::swap(t1, t2);
  }

  return true;

}

bool Sphere::has_intersection(const Ray &r) const {

  // TODO (Part 1.4):
  // Implement ray - sphere intersection.
  // Note that you might want to use the the Sphere::test helper here.
  double t1, t2;
  if (!test(r, t1, t2)) {
    return false;
  }

  double t = INF_D;
  if (t1 >= r.min_t && t1 <= r.max_t) {
    t = t1;
  } else if (t2 >= r.min_t && t2 <= r.max_t) {
    t = t2;
  } else {
    return false;
  }

  r.max_t = t;
  return true;
}

bool Sphere::intersect(const Ray &r, Intersection *i) const {

  // TODO (Part 1.4):
  // Implement ray - sphere intersection.
  // Note again that you might want to use the the Sphere::test helper here.
  // When an intersection takes place, the Intersection data should be updated
  // correspondingly.
  double t1, t2;
  if (!test(r, t1, t2)) {
    return false;
  }

  double t = INF_D;
  if (t1 >= r.min_t && t1 <= r.max_t) {
    t = t1;
  } else if (t2 >= r.min_t && t2 <= r.max_t) {
    t = t2;
  } else {
    return false;
  }

  r.max_t = t;
  i->t = t;
  i->n = normal(r.at_time(t));
  i->primitive = this;
  i->bsdf = get_bsdf();
  return true;
}

void Sphere::draw(const Color &c, float alpha) const {
  Misc::draw_sphere_opengl(o, r, c);
}

void Sphere::drawOutline(const Color &c, float alpha) const {
  // Misc::draw_sphere_opengl(o, r, c);
}

} // namespace SceneObjects
} // namespace CGL
