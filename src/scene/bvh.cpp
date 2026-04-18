#include "bvh.h"

#include "CGL/CGL.h"
#include "triangle.h"

#include <iostream>
#include <stack>

using namespace std;

namespace CGL {
namespace SceneObjects {

BVHAccel::BVHAccel(const std::vector<Primitive *> &_primitives,
                   size_t max_leaf_size) {

  primitives = std::vector<Primitive *>(_primitives);
  root = construct_bvh(primitives.begin(), primitives.end(), max_leaf_size);
}

BVHAccel::~BVHAccel() {
  if (root)
    delete root;
  primitives.clear();
}

BBox BVHAccel::get_bbox() const { return root->bb; }

void BVHAccel::draw(BVHNode *node, const Color &c, float alpha) const {
  if (node->isLeaf()) {
    for (auto p = node->start; p != node->end; p++) {
      (*p)->draw(c, alpha);
    }
  } else {
    draw(node->l, c, alpha);
    draw(node->r, c, alpha);
  }
}

void BVHAccel::drawOutline(BVHNode *node, const Color &c, float alpha) const {
  if (node->isLeaf()) {
    for (auto p = node->start; p != node->end; p++) {
      (*p)->drawOutline(c, alpha);
    }
  } else {
    drawOutline(node->l, c, alpha);
    drawOutline(node->r, c, alpha);
  }
}

BVHNode *BVHAccel::construct_bvh(std::vector<Primitive *>::iterator start,
                                 std::vector<Primitive *>::iterator end,
                                 size_t max_leaf_size) {

  // TODO (Part 2.1):
  // Construct a BVH from the given vector of primitives and maximum leaf
  // size configuration. The starter code build a BVH aggregate with a
  // single leaf node (which is also the root) that encloses all the
  // primitives.

  BBox bbox;
  BBox centroid_bbox;

  for (auto p = start; p != end; p++) {
    const BBox bb = (*p)->get_bbox();
    bbox.expand(bb);
    centroid_bbox.expand(bb.centroid());
  }

  BVHNode *node = new BVHNode(bbox);
  const size_t num_primitives = end - start;

  if (num_primitives <= max_leaf_size) {
    node->start = start;
    node->end = end;
    return node;
  }

  int axis = 0;
  if (centroid_bbox.extent.y > centroid_bbox.extent[axis]) axis = 1;
  if (centroid_bbox.extent.z > centroid_bbox.extent[axis]) axis = 2;

  const double split = centroid_bbox.centroid()[axis];
  auto mid = std::partition(start, end, [axis, split](Primitive *primitive) {
    return primitive->get_bbox().centroid()[axis] < split;
  });

  if (mid == start || mid == end) {
    mid = start + num_primitives / 2;
    std::nth_element(start, mid, end, [axis](Primitive *a, Primitive *b) {
      return a->get_bbox().centroid()[axis] < b->get_bbox().centroid()[axis];
    });
  }

  node->l = construct_bvh(start, mid, max_leaf_size);
  node->r = construct_bvh(mid, end, max_leaf_size);
  return node;


}

bool BVHAccel::has_intersection(const Ray &ray, BVHNode *node) const {
  // TODO (Part 2.3):
  // Fill in the intersect function.
  // Take note that this function has a short-circuit that the
  // Intersection version cannot, since it returns as soon as it finds
  // a hit, it doesn't actually have to find the closest hit.

  double t0 = ray.min_t;
  double t1 = ray.max_t;
  if (!node->bb.intersect(ray, t0, t1)) {
    return false;
  }

  if (node->isLeaf()) {
    for (auto p = node->start; p != node->end; ++p) {
      total_isects++;
      if ((*p)->has_intersection(ray)) {
        return true;
      }
    }
    return false;
  }

  BVHNode *first = node->l;
  BVHNode *second = node->r;
  double first_t0 = ray.min_t, first_t1 = ray.max_t;
  double second_t0 = ray.min_t, second_t1 = ray.max_t;
  bool hit_first_box = first && first->bb.intersect(ray, first_t0, first_t1);
  bool hit_second_box = second && second->bb.intersect(ray, second_t0, second_t1);

  if (!hit_first_box && !hit_second_box) {
    return false;
  }

  if (hit_first_box && hit_second_box && second_t0 < first_t0) {
    std::swap(first, second);
    std::swap(hit_first_box, hit_second_box);
  }

  if (hit_first_box && has_intersection(ray, first)) {
    return true;
  }
  if (hit_second_box && has_intersection(ray, second)) {
    return true;
  }

  return false;


}

bool BVHAccel::intersect(const Ray &ray, Intersection *i, BVHNode *node) const {
  // TODO (Part 2.3):
  // Fill in the intersect function.

  double t0 = ray.min_t;
  double t1 = ray.max_t;
  if (!node->bb.intersect(ray, t0, t1)) {
    return false;
  }

  if (node->isLeaf()) {
    bool hit = false;
    for (auto p = node->start; p != node->end; ++p) {
      total_isects++;
      hit = (*p)->intersect(ray, i) || hit;
    }
    return hit;
  }

  BVHNode *first = node->l;
  BVHNode *second = node->r;
  double first_t0 = ray.min_t, first_t1 = ray.max_t;
  double second_t0 = ray.min_t, second_t1 = ray.max_t;
  bool hit_first_box = first && first->bb.intersect(ray, first_t0, first_t1);
  bool hit_second_box = second && second->bb.intersect(ray, second_t0, second_t1);

  if (!hit_first_box && !hit_second_box) {
    return false;
  }

  if (hit_first_box && hit_second_box && second_t0 < first_t0) {
    std::swap(first, second);
    std::swap(hit_first_box, hit_second_box);
  }

  bool hit = false;
  if (hit_first_box) {
    hit = intersect(ray, i, first) || hit;
  }
  if (hit_second_box) {
    hit = intersect(ray, i, second) || hit;
  }
  return hit;


}

} // namespace SceneObjects
} // namespace CGL
