#include "bsdf.h"

#include <algorithm>
#include <iostream>
#include <utility>

#include "application/visual_debugger.h"
#include "util/random_util.h"

using std::max;
using std::min;
using std::swap;

namespace CGL {

namespace {

double dielectric_fresnel(double cos_i, double eta_i, double eta_t) {
  cos_i = clamp(fabs(cos_i), 0.0, 1.0);
  const double eta = eta_i / eta_t;
  const double sin_t2 = eta * eta * max(0.0, 1.0 - cos_i * cos_i);
  if (sin_t2 >= 1.0) return 1.0;

  const double cos_t = sqrt(max(0.0, 1.0 - sin_t2));
  const double r_parallel =
      (eta_t * cos_i - eta_i * cos_t) / (eta_t * cos_i + eta_i * cos_t);
  const double r_perpendicular =
      (eta_i * cos_i - eta_t * cos_t) / (eta_i * cos_i + eta_t * cos_t);
  return 0.5 * (r_parallel * r_parallel +
                r_perpendicular * r_perpendicular);
}

}  // namespace

// Mirror BSDF //

Vector3D MirrorBSDF::f(const Vector3D wo, const Vector3D wi) {
  return Vector3D();
}

Vector3D MirrorBSDF::sample_f(const Vector3D wo, Vector3D* wi, double* pdf) {

  reflect(wo, wi);
  *pdf = 1.0;
  return reflectance / abs_cos_theta(*wi);
}

void MirrorBSDF::render_debugger_node()
{
  if (ImGui::TreeNode(this, "Mirror BSDF"))
  {
    DragDouble3("Reflectance", &reflectance[0], 0.005);
    ImGui::TreePop();
  }
}

// Microfacet BSDF //

double MicrofacetBSDF::G(const Vector3D wo, const Vector3D wi) {
  return 1.0 / (1.0 + Lambda(wi) + Lambda(wo));
}

double MicrofacetBSDF::D(const Vector3D h) {
  // TODO: proj3-2, part 3
  // Compute Beckmann normal distribution function (NDF) here.
  // You will need the roughness alpha.
  
  return 1.0;
}

Vector3D MicrofacetBSDF::F(const Vector3D wi) {
  // TODO: proj3-2, part 3
  // Compute Fresnel term for reflection on dielectric-conductor interface.
  // You will need both eta and etaK, both of which are Vector3D.

  double cosTheta = cos_theta(wi);
  
  return Vector3D();
}

Vector3D MicrofacetBSDF::f(const Vector3D wo, const Vector3D wi) {
  // TODO: proj3-2, part 3
  // Implement microfacet model here.

  return Vector3D();
}

Vector3D MicrofacetBSDF::sample_f(const Vector3D wo, Vector3D* wi, double* pdf) {
  // TODO: proj3-2, part 3
  // *Importance* sample Beckmann normal distribution function (NDF) here.
  // Note: You should fill in the sampled direction *wi and the corresponding *pdf,
  //       and return the sampled BRDF value.



  *wi = cosineHemisphereSampler.get_sample(pdf);

  return MicrofacetBSDF::f(wo, *wi);
}

void MicrofacetBSDF::render_debugger_node()
{
  if (ImGui::TreeNode(this, "Micofacet BSDF"))
  {
    DragDouble3("eta", &eta[0], 0.005);
    DragDouble3("K", &k[0], 0.005);
    DragDouble("alpha", &alpha, 0.005);
    ImGui::TreePop();
  }
}

// Refraction BSDF //

Vector3D RefractionBSDF::f(const Vector3D wo, const Vector3D wi) {
  return Vector3D();
}

Vector3D RefractionBSDF::sample_f(const Vector3D wo, Vector3D* wi, double* pdf) {

  if (!refract(wo, wi, ior)) {
    *pdf = 0.0;
    return Vector3D();
  }

  *pdf = 1.0;
  return transmittance / abs_cos_theta(*wi);
}

void RefractionBSDF::render_debugger_node()
{
  if (ImGui::TreeNode(this, "Refraction BSDF"))
  {
    DragDouble3("Transmittance", &transmittance[0], 0.005);
    DragDouble("ior", &ior, 0.005);
    ImGui::TreePop();
  }
}

// Glass BSDF //

Vector3D GlassBSDF::f(const Vector3D wo, const Vector3D wi) {
  return Vector3D();
}

Vector3D GlassBSDF::sample_f(const Vector3D wo, Vector3D* wi, double* pdf) {

  const double eta_i = wo.z > 0.0 ? 1.0 : ior;
  const double eta_t = wo.z > 0.0 ? ior : 1.0;
  const double fr = dielectric_fresnel(wo.z, eta_i, eta_t);

  Vector3D wt;
  const bool can_refract = refract(wo, &wt, ior);
  if (!can_refract || coin_flip(fr)) {
    reflect(wo, wi);
    *pdf = can_refract ? fr : 1.0;
    return reflectance * (*pdf) / abs_cos_theta(*wi);
  }

  *wi = wt;
  *pdf = 1.0 - fr;
  return transmittance * (*pdf) / abs_cos_theta(*wi);
}

void GlassBSDF::render_debugger_node()
{
  if (ImGui::TreeNode(this, "Refraction BSDF"))
  {
    DragDouble3("Reflectance", &reflectance[0], 0.005);
    DragDouble3("Transmittance", &transmittance[0], 0.005);
    DragDouble("ior", &ior, 0.005);
    ImGui::TreePop();
  }
}

void BSDF::reflect(const Vector3D wo, Vector3D* wi) {

  *wi = Vector3D(-wo.x, -wo.y, wo.z);
}

bool BSDF::refract(const Vector3D wo, Vector3D* wi, double ior) {

  const bool entering = wo.z > 0.0;
  const double eta = entering ? 1.0 / ior : ior;
  const double cos_i = fabs(wo.z);
  const double sin_t2 = eta * eta * max(0.0, 1.0 - cos_i * cos_i);
  if (sin_t2 >= 1.0) return false;

  const double cos_t = sqrt(max(0.0, 1.0 - sin_t2));
  *wi = Vector3D(-eta * wo.x,
                 -eta * wo.y,
                 entering ? -cos_t : cos_t);
  return true;

}

} // namespace CGL
