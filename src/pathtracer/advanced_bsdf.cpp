#include "bsdf.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <utility>

#include "application/visual_debugger.h"
#include "util/random_util.h"

using std::max;
using std::min;
using std::swap;

namespace CGL {

// Mirror BSDF //

Vector3D MirrorBSDF::f(const Vector3D wo, const Vector3D wi) {
  return Vector3D();
}

Vector3D MirrorBSDF::sample_f(const Vector3D wo, Vector3D* wi, double* pdf) {
  reflect(wo, wi);
  *pdf = 1.0;
  // Cancel the cos θ factor that the integrator divides by, so the
  // returned reflectance equals exactly the requested reflectance after
  // the standard f * cos θ / pdf weighting.
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
  // Radiance-scaling factor (η_t / η_i)^2 for transmission. This accounts
  // for the change in solid angle across the refraction.
  const double eta = (wo.z > 0.0) ? (1.0 / ior) : ior;
  return transmittance / abs_cos_theta(*wi) / (eta * eta);
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
  // Try to refract first. TIR -> always reflect.
  Vector3D wi_t;
  if (!refract(wo, &wi_t, ior)) {
    reflect(wo, wi);
    *pdf = 1.0;
    return reflectance / abs_cos_theta(*wi);
  }

  // Schlick's approximation to the unpolarised Fresnel reflectance.
  const double R0_root = (1.0 - ior) / (1.0 + ior);
  const double R0 = R0_root * R0_root;
  const double cos_i = std::abs(wo.z);
  const double one_minus_c = 1.0 - cos_i;
  const double R = R0 + (1.0 - R0) * one_minus_c * one_minus_c
                                   * one_minus_c * one_minus_c
                                   * one_minus_c;

  // Russian-roulette pick of one of the two delta lobes, using R as the
  // reflection probability. With probability R sample reflection; with
  // probability (1-R) sample refraction. Either way the integrator
  // divides by the chosen pdf, so the radiance contribution is correctly
  // weighted by R (or 1-R) once.
  if (coin_flip(R)) {
    reflect(wo, wi);
    *pdf = R;
    return R * reflectance / abs_cos_theta(*wi);
  } else {
    *wi = wi_t;
    *pdf = 1.0 - R;
    const double eta = (wo.z > 0.0) ? (1.0 / ior) : ior;
    return (1.0 - R) * transmittance / abs_cos_theta(*wi) / (eta * eta);
  }
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

// All directions here are in the BSDF's local frame where the surface normal
// is (0, 0, 1). Reflect about the z = 0 plane: flip the lateral components
// and keep z (cos θ) unchanged.
void BSDF::reflect(const Vector3D wo, Vector3D* wi) {
  *wi = Vector3D(-wo.x, -wo.y, wo.z);
}

// Snell's law in the local frame, with full handling of total internal
// reflection. Convention: wo points away from the surface (toward the
// camera/incoming-ray-source). If wo.z > 0 we are entering the medium from
// vacuum (η_o = 1, η_i = ior); otherwise we are leaving the medium back into
// vacuum (η_o = ior, η_i = 1). Returns false on TIR — caller is expected to
// fall back to perfect reflection in that case.
bool BSDF::refract(const Vector3D wo, Vector3D* wi, double ior) {
  const bool entering = wo.z > 0.0;
  const double eta = entering ? (1.0 / ior) : ior;     // η_t / η_i ratio
  const double cos_o = wo.z;                            // signed cos w.r.t. n
  const double sin2_t = eta * eta * (1.0 - cos_o * cos_o);
  if (sin2_t >= 1.0) return false;                     // total internal reflection
  const double cos_t = std::sqrt(1.0 - sin2_t);
  // Refracted direction in the local frame. The transmitted z is in the
  // opposite hemisphere from wo (we cross the surface), and the lateral
  // components are scaled by η.
  const double sign_z = entering ? -1.0 : 1.0;
  *wi = Vector3D(-eta * wo.x, -eta * wo.y, sign_z * cos_t);
  return true;
}

} // namespace CGL
