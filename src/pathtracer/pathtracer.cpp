#include "pathtracer.h"

#include "scene/light.h"
#include "scene/sphere.h"
#include "scene/triangle.h"
#include "pathtracer/sampler.h"
#include "util/random_util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <limits>

using namespace CGL::SceneObjects;

namespace CGL {

namespace {

double read_env_double(const char* key, double def) {
  const char* v = std::getenv(key);
  return v ? std::atof(v) : def;
}

int read_env_int(const char* key, int def) {
  const char* v = std::getenv(key);
  return v ? std::atoi(v) : def;
}

double clamp01(double v) {
  return std::max(0.0, std::min(1.0, v));
}

double luminance(const Vector3D& v) {
  return 0.2126 * v.x + 0.7152 * v.y + 0.0722 * v.z;
}

Vector3D exp_transmittance(const Vector3D& sigma_t, double d) {
  if (d <= 0.0) return Vector3D(1.0);
  return Vector3D(std::exp(-sigma_t.x * d),
                  std::exp(-sigma_t.y * d),
                  std::exp(-sigma_t.z * d));
}

bool nearly_black(const Vector3D& v) {
  return v.x <= 1e-10 && v.y <= 1e-10 && v.z <= 1e-10;
}

double halton(int index, int base) {
  double f = 1.0;
  double r = 0.0;
  while (index > 0) {
    f /= base;
    r += f * (index % base);
    index /= base;
  }
  return r;
}

Vector3D sample_hg_direction(const Vector3D& forward, double g) {
  const double u1 = random_uniform();
  const double u2 = random_uniform();
  double cos_theta;

  if (std::abs(g) < 1e-4) {
    cos_theta = 1.0 - 2.0 * u1;
  } else {
    const double sqr = (1.0 - g * g) / (1.0 - g + 2.0 * g * u1);
    cos_theta = (1.0 + g * g - sqr * sqr) / (2.0 * g);
    cos_theta = std::max(-1.0, std::min(1.0, cos_theta));
  }

  const double sin_theta = std::sqrt(std::max(0.0, 1.0 - cos_theta * cos_theta));
  const double phi = 2.0 * PI * u2;
  const Vector3D local(std::cos(phi) * sin_theta,
                       std::sin(phi) * sin_theta,
                       cos_theta);

  Matrix3x3 basis;
  make_coord_space(basis, forward.unit());
  return (basis * local).unit();
}

bool refract_air_to_water(const Vector3D& d_air,
                          const Vector3D& n_air,
                          double eta_t,
                          Vector3D* d_water) {
  const double eta = 1.0 / eta_t;
  const double cos_i = -dot(n_air, d_air);
  if (cos_i <= 0.0) return false;

  const double sin_t2 = eta * eta * std::max(0.0, 1.0 - cos_i * cos_i);
  if (sin_t2 >= 1.0) return false;

  const double cos_t = std::sqrt(std::max(0.0, 1.0 - sin_t2));
  *d_water = (eta * d_air + (eta * cos_i - cos_t) * n_air).unit();
  return true;
}

Vector3D water_boundary_normal(const Medium* medium, double x, double z) {
  if (!medium || medium->boundary_amp <= 0.0) return Vector3D(0.0, 1.0, 0.0);

  const double amp = medium->boundary_amp;
  const double k1 = medium->boundary_freq;
  const double k2 = medium->boundary_freq * 1.7;
  const double p1 = 0.31, p2 = 1.27, p3 = 2.11;

  const double dhdx = amp * (k1 * std::cos(k1 * x + p1) * std::cos(k1 * z + p2)
                           + 0.5 * k2 * std::cos(k2 * (x + z) + p3));
  const double dhdz = amp * (-k1 * std::sin(k1 * x + p1) * std::sin(k1 * z + p2)
                           + 0.5 * k2 * std::cos(k2 * (x + z) + p3));
  return Vector3D(-dhdx, 1.0, -dhdz).unit();
}

double water_boundary_y(const Medium* medium, double x, double z) {
  if (!medium || !medium->bounds) return 0.0;
  return medium->bounds->max.y +
         medium->boundary_amp * medium->boundary_height(x, z);
}

bool is_transmissive_shadow_surface(const BSDF* bsdf) {
  return dynamic_cast<const GlassBSDF*>(bsdf) ||
         dynamic_cast<const RefractionBSDF*>(bsdf);
}

bool point_to_cell(const PathTracer::VolumetricPhotonMap& map,
                   const Vector3D& p,
                   int* ix,
                   int* iy,
                   int* iz) {
  if (map.bounds.empty()) return false;
  const Vector3D rel = (p - map.bounds.min) / map.bounds.extent;
  if (rel.x < 0.0 || rel.x > 1.0 ||
      rel.y < 0.0 || rel.y > 1.0 ||
      rel.z < 0.0 || rel.z > 1.0) {
    return false;
  }
  *ix = std::max(0, std::min(map.nx - 1, static_cast<int>(rel.x * map.nx)));
  *iy = std::max(0, std::min(map.ny - 1, static_cast<int>(rel.y * map.ny)));
  *iz = std::max(0, std::min(map.nz - 1, static_cast<int>(rel.z * map.nz)));
  return true;
}

void insert_volume_photon(PathTracer::VolumetricPhotonMap& map,
                          const PathTracer::VolumetricPhotonMap::Photon& photon) {
  int ix, iy, iz;
  if (!point_to_cell(map, photon.position, &ix, &iy, &iz)) return;

  const int index = static_cast<int>(map.photons.size());
  map.photons.push_back(photon);
  map.cells[map.index(ix, iy, iz)].push_back(index);
}

Vector3D gather_volume_photons(const PathTracer::VolumetricPhotonMap& map,
                               const Vector3D& p,
                               const Vector3D& wo,
                               double hg_g) {
  if (!map.valid()) return Vector3D();

  int cx, cy, cz;
  if (!point_to_cell(map, p, &cx, &cy, &cz)) return Vector3D();

  const double cell_x = map.bounds.extent.x / map.nx;
  const double cell_y = map.bounds.extent.y / map.ny;
  const double cell_z = map.bounds.extent.z / map.nz;
  const int rx = std::max(1, static_cast<int>(std::ceil(map.radius / cell_x)));
  const int ry = std::max(1, static_cast<int>(std::ceil(map.radius / cell_y)));
  const int rz = std::max(1, static_cast<int>(std::ceil(map.radius / cell_z)));
  const double radius2 = map.radius * map.radius;

  Vector3D power;
  for (int iy = std::max(0, cy - ry); iy <= std::min(map.ny - 1, cy + ry); ++iy) {
    for (int iz = std::max(0, cz - rz); iz <= std::min(map.nz - 1, cz + rz); ++iz) {
      for (int ix = std::max(0, cx - rx); ix <= std::min(map.nx - 1, cx + rx); ++ix) {
        const std::vector<int>& cell = map.cells[map.index(ix, iy, iz)];
        for (int photon_id : cell) {
          const PathTracer::VolumetricPhotonMap::Photon& photon =
              map.photons[photon_id];
          if ((photon.position - p).norm2() > radius2) continue;
          power += photon.power * phase_hg(dot(photon.wi, wo), hg_g);
        }
      }
    }
  }

  const double sphere_volume = (4.0 / 3.0) * PI * map.radius * map.radius * map.radius;
  return sphere_volume > 0.0 ? power * (map.strength / sphere_volume)
                             : Vector3D();
}

}  // namespace

PathTracer::PathTracer() {
  gridSampler = new UniformGridSampler2D();
  hemisphereSampler = new UniformHemisphereSampler3D();

  tm_gamma = 2.2f;
  tm_level = 1.0f;
  tm_key = 0.18;
  tm_wht = 5.0f;
}

PathTracer::~PathTracer() {
  delete gridSampler;
  delete hemisphereSampler;
}

void PathTracer::set_frame_size(size_t width, size_t height) {
  sampleBuffer.resize(width, height);
  sampleCountBuffer.resize(width * height);
}

void PathTracer::clear() {
  bvh = NULL;
  scene = NULL;
  camera = NULL;
  volume_photon_map.clear();
  sampleBuffer.clear();
  sampleCountBuffer.clear();
  sampleBuffer.resize(0, 0);
  sampleCountBuffer.resize(0, 0);
}

bool PathTracer::shadow_ray_blocked(const Ray& r) const {
  Ray shadow = r;
  double remaining = r.max_t;

  // Treat ideal transmissive surfaces as transparent to shadow rays. This is
  // an approximation for direct/volume light sampling: it avoids letting the
  // water interface become an opaque blocker while the actual camera/specular
  // paths still use the glass BSDF and Snell refraction.
  for (int skipped = 0; skipped < 16; ++skipped) {
    shadow.max_t = remaining;
    Intersection isect;
    if (!bvh->intersect(shadow, &isect)) return false;
    if (!is_transmissive_shadow_surface(isect.bsdf)) return true;

    const double step = isect.t + EPS_F;
    if (step >= remaining) return false;
    shadow.o = shadow.o + shadow.d * step;
    shadow.min_t = EPS_F;
    remaining -= step;
  }

  return true;
}

void PathTracer::write_to_framebuffer(ImageBuffer &framebuffer, size_t x0,
                                      size_t y0, size_t x1, size_t y1) {
  sampleBuffer.toColor(framebuffer, x0, y0, x1, y1);
}

Vector3D
PathTracer::estimate_direct_lighting_hemisphere(const Ray &r,
                                                const Intersection &isect) {
  // Estimate the lighting from this intersection coming directly from a light.
  // For this function, sample uniformly in a hemisphere.

  // Note: When comparing Cornel Box (CBxxx.dae) results to importance sampling, you may find the "glow" around the light source is gone.
  // This is totally fine: the area lights in importance sampling has directionality, however in hemisphere sampling we don't model this behaviour.

  // make a coordinate system for a hit point
  // with N aligned with the Z direction.
  Matrix3x3 o2w;
  make_coord_space(o2w, isect.n);
  Matrix3x3 w2o = o2w.T();

  // w_out points towards the source of the ray (e.g.,
  // toward the camera if this is a primary ray)
  const Vector3D hit_p = r.o + r.d * isect.t;
  const Vector3D w_out = w2o * (-r.d);

  // This is the same number of total samples as
  // estimate_direct_lighting_importance (outside of delta lights). We keep the
  // same number of samples for clarity of comparison.
  int num_samples = scene->lights.size() * ns_area_light;
  Vector3D L_out;

  if (num_samples == 0) {
    return L_out;
  }

  // TODO (Part 3): Write your sampling loop here
  // TODO BEFORE YOU BEGIN
  // UPDATE `est_radiance_global_illumination` to return direct lighting instead of normal shading 
  const double pdf = 1.0 / (2.0 * PI);

  for (int i = 0; i < num_samples; ++i) {
    const Vector3D wi = hemisphereSampler->get_sample();
    const Vector3D wi_world = o2w * wi;

    Ray shadow_ray(hit_p, wi_world);
    shadow_ray.min_t = EPS_F;

    Intersection light_isect;
    bool hit_light_path = false;
    for (int skipped = 0; skipped < 16; ++skipped) {
      if (!bvh->intersect(shadow_ray, &light_isect)) break;
      if (!is_transmissive_shadow_surface(light_isect.bsdf)) {
        hit_light_path = true;
        break;
      }
      shadow_ray.o = shadow_ray.o + shadow_ray.d * (light_isect.t + EPS_F);
      shadow_ray.min_t = EPS_F;
      shadow_ray.max_t = INF_D;
    }
    if (!hit_light_path) continue;

    const Vector3D emission = light_isect.bsdf->get_emission();
    if (emission == Vector3D()) {
      continue;
    }

    L_out += isect.bsdf->f(w_out, wi) * emission * abs_cos_theta(wi) / pdf;
  }

  return L_out / num_samples;

}

Vector3D
PathTracer::estimate_direct_lighting_importance(const Ray &r,
                                                const Intersection &isect) {
  // Estimate the lighting from this intersection coming directly from a light.
  // To implement importance sampling, sample only from lights, not uniformly in
  // a hemisphere.

  // make a coordinate system for a hit point
  // with N aligned with the Z direction.
  Matrix3x3 o2w;
  make_coord_space(o2w, isect.n);
  Matrix3x3 w2o = o2w.T();

  // w_out points towards the source of the ray (e.g.,
  // toward the camera if this is a primary ray)
  const Vector3D hit_p = r.o + r.d * isect.t;
  const Vector3D w_out = w2o * (-r.d);
  Vector3D L_out;

  for (SceneLight *light : scene->lights) {
    const int num_samples = light->is_delta_light() ? 1 : ns_area_light;
    Vector3D light_contrib;

    for (int sample_idx = 0; sample_idx < num_samples; ++sample_idx) {
      Vector3D wi_world;
      double dist_to_light;
      double pdf;
      const Vector3D radiance = light->sample_L(hit_p, &wi_world, &dist_to_light, &pdf);

      if (pdf <= 0.0 || radiance == Vector3D()) {
        continue;
      }

      const Vector3D wi = w2o * wi_world;
      if (wi.z <= 0.0) {
        continue;
      }

      Ray shadow_ray(hit_p, wi_world);
      shadow_ray.min_t = EPS_F;
      shadow_ray.max_t = dist_to_light - EPS_F;

      if (shadow_ray_blocked(shadow_ray)) {
        continue;
      }

      Vector3D tr = medium ? medium->ray_transmittance(shadow_ray, dist_to_light)
                           : Vector3D(1.0);
      light_contrib += tr * isect.bsdf->f(w_out, wi) * radiance * abs_cos_theta(wi) / pdf;
    }

    L_out += light_contrib / num_samples;
  }

  return L_out;

}

// ---------------------------------------------------------------------------
// Equiangular + distance-sampling MIS integrator for volumetric single-scatter
// direct lighting along a camera ray segment inside the participating medium.
//
// For each light sample, we combine two sampling strategies over the segment:
//   (a) equiangular sampling — density concentrated near the light; excellent
//       for point / spot / area lights (god-ray peaks).
//   (b) truncated-exponential sampling — matches Beer-Lambert falloff; good
//       for "close" scatter near the camera.
// Contributions are weighted with the two-strategy power heuristic (β = 2).
//
// Directional / infinite-distance lights are not amenable to equiangular
// sampling (no finite light position); for those we fall back to the
// truncated-exponential estimator alone.
//
// Integrand for one light is the in-scattering integral:
//   ∫_{t_enter}^{t_exit}
//     T_cam(t) · σ_s · p(ω, ω') · T_sh(p_scat → p_light) · L_i / pdf_light  dt
Vector3D PathTracer::estimate_vol_direct_lighting_mis(const Ray& r,
                                                      double t_enter,
                                                      double t_exit) {
  Vector3D L_out;
  if (!medium) return L_out;
  if (t_exit <= t_enter) return L_out;

  const double sigma_avg = medium->sigma_t_avg();
  const Vector3D sigma_s = medium->sigma_s;
  const double  hg_g     = medium->hg_g;
  const double  seg     = t_exit - t_enter;
  const Vector3D ref_p  = r.o + r.d * (t_enter + 0.5 * seg);
  const bool    dt_valid = sigma_avg > 1e-8;

  // Helper: single-scatter direct-lighting integrand at ray distance t, using
  // a specific light-point sample (p_light) whose radiance we already have.
  auto eval_at = [&](double t, const Vector3D& p_light,
                     const Vector3D& radiance,
                     double pdf_light) -> Vector3D {
    if (t <= t_enter || t >= t_exit) return Vector3D();
    const Vector3D p_scat = r.o + t * r.d;
    const Vector3D to_light = p_light - p_scat;
    const double   dist = to_light.norm();
    if (dist < EPS_F) return Vector3D();
    const Vector3D wi = to_light / dist;

    Ray shadow_ray(p_scat, wi);
    shadow_ray.min_t = EPS_F;
    shadow_ray.max_t = dist - EPS_F;
    if (shadow_ray_blocked(shadow_ray)) return Vector3D();

    const Vector3D T_cam = medium->det_transmittance(r, t_enter, t);
    const Vector3D T_sh  = medium->ray_transmittance(shadow_ray, dist);
    // Henyey-Greenstein phase: cos θ between incoming (light → scatter,
    // i.e. -wi) and outgoing (scatter → camera, i.e. -r.d) is dot(wi, r.d).
    const double phase_f = phase_hg(dot(wi, r.d), hg_g);
    return (T_cam * sigma_s * T_sh * radiance) * (phase_f / pdf_light);
  };

  for (SceneLight* light : scene->lights) {
    const int num_samples = light->is_delta_light() ? 1 : ns_area_light;
    Vector3D light_contrib;

    for (int i = 0; i < num_samples; ++i) {
      // Draw a light sample from the midpoint of the segment. For area lights
      // this fixes a specific point on the light that both sampling strategies
      // target during MIS.
      Vector3D wi_world;
      double dist_to_light, pdf_light;
      const Vector3D radiance =
          light->sample_L(ref_p, &wi_world, &dist_to_light, &pdf_light);
      if (pdf_light <= 0.0 || radiance == Vector3D()) continue;

      // Classify light by finite vs. infinite distance.
      const bool finite_light = dist_to_light < 1e6;
      const Vector3D p_light  = finite_light
                                   ? ref_p + wi_world * dist_to_light
                                   : Vector3D();  // unused for infinite lights

      if (!finite_light) {
        // ----- Directional / infinite-distance: distance sampling only -----
        if (!dt_valid) continue;
        double pdf_dt;
        const double t = truncexp_sample(sigma_avg, t_enter, t_exit,
                                         random_uniform(), &pdf_dt);
        const Vector3D p_scat = r.o + t * r.d;

        // Re-sample the light at the actual scatter point so the direction /
        // distance used for the shadow ray is consistent.
        Vector3D wi_s; double d_s, pdf_s;
        const Vector3D rad_s =
            light->sample_L(p_scat, &wi_s, &d_s, &pdf_s);
        if (pdf_s <= 0.0 || rad_s == Vector3D()) continue;
        Ray shadow(p_scat, wi_s);
        shadow.min_t = EPS_F;
        shadow.max_t = d_s - EPS_F;
        if (shadow_ray_blocked(shadow)) continue;

        const Vector3D T_cam = medium->det_transmittance(r, t_enter, t);
        const Vector3D T_sh  = medium->ray_transmittance(shadow, d_s);
        const double phase_f = phase_hg(dot(wi_s, r.d), hg_g);
        light_contrib += (T_cam * sigma_s * T_sh * rad_s)
                         * (phase_f / (pdf_s * pdf_dt));
        continue;
      }

      // ----- Finite-distance: equiangular + truncated-exp MIS -----
      EquiangularSampler eq(r.o, r.d, p_light, t_enter, t_exit);

      if (!eq.valid) {
        // Degenerate geometry (ray nearly through light); fall back to
        // distance sampling alone.
        if (!dt_valid) continue;
        double pdf_dt;
        const double t = truncexp_sample(sigma_avg, t_enter, t_exit,
                                         random_uniform(), &pdf_dt);
        light_contrib += eval_at(t, p_light, radiance, pdf_light) / pdf_dt;
        continue;
      }

      // Strategy (a): equiangular
      {
        double pdf_eq;
        const double t_eq = eq.sample(random_uniform(), &pdf_eq);
        if (pdf_eq > 0.0 && t_eq > t_enter && t_eq < t_exit) {
          const double pdf_dt_at_eq =
              dt_valid ? truncexp_pdf(sigma_avg, t_enter, t_exit, t_eq) : 0.0;
          const double w_eq = mis_power2(pdf_eq, pdf_dt_at_eq);
          light_contrib +=
              w_eq * eval_at(t_eq, p_light, radiance, pdf_light) / pdf_eq;
        }
      }

      // Strategy (b): truncated exponential
      if (dt_valid) {
        double pdf_dt;
        const double t_dt = truncexp_sample(sigma_avg, t_enter, t_exit,
                                            random_uniform(), &pdf_dt);
        if (pdf_dt > 0.0) {
          const double pdf_eq_at_dt = eq.pdf_at(t_dt);
          const double w_dt = mis_power2(pdf_dt, pdf_eq_at_dt);
          light_contrib +=
              w_dt * eval_at(t_dt, p_light, radiance, pdf_light) / pdf_dt;
        }
      }
    }

    L_out += light_contrib / num_samples;
  }
  return L_out;
}

void PathTracer::build_volume_photon_map() {
  volume_photon_map.clear();

  if (!medium || !medium->bounds || !scene || !bvh) return;
  if (read_env_int("VPM_ENABLE", 1) == 0) return;

  const int res = std::max(4, read_env_int("VPM_GRID_RES", 48));
  const int nx = std::max(4, read_env_int("VPM_GRID_RES_X", res));
  const int ny = std::max(4, read_env_int("VPM_GRID_RES_Y", std::max(4, res / 2)));
  const int nz = std::max(4, read_env_int("VPM_GRID_RES_Z", res));
  const int photon_count = std::max(0, read_env_int("VPM_PHOTONS", 80000));
  if (photon_count == 0) return;

  std::vector<const AreaLight*> area_lights;
  for (SceneLight* light : scene->lights) {
    if (const AreaLight* area = dynamic_cast<const AreaLight*>(light)) {
      area_lights.push_back(area);
    }
  }
  if (area_lights.empty()) return;

  volume_photon_map.enabled = true;
  volume_photon_map.nx = nx;
  volume_photon_map.ny = ny;
  volume_photon_map.nz = nz;
  volume_photon_map.bounds = *medium->bounds;
  volume_photon_map.radius = read_env_double("VPM_RADIUS", 0.08);
  volume_photon_map.strength = read_env_double("VPM_STRENGTH", 1.0);
  volume_photon_map.photons.reserve(photon_count);
  volume_photon_map.cells.assign(nx * ny * nz, std::vector<int>());

  const int max_depth = std::max(1, read_env_int("VPM_MAX_DEPTH", 8));
  const int rr_start = std::max(1, read_env_int("VPM_RR_START", 3));
  const Vector3D sigma_t = medium->sigma_t();
  const Vector3D albedo = medium->albedo_c();
  const double sigma_avg = medium->sigma_t_avg();
  const double surface_area =
      volume_photon_map.bounds.extent.x * volume_photon_map.bounds.extent.z;

  int launched = 0;
  for (int i = 1; i <= photon_count; ++i) {
    const AreaLight* light = area_lights[(i - 1) % area_lights.size()];

    const double ul = halton(i, 2) - 0.5;
    const double vl = halton(i, 3) - 0.5;
    const double us = halton(i, 5);
    const double vs = halton(i, 7);

    const Vector3D p_light = light->position + ul * light->dim_x + vl * light->dim_y;
    const double x = volume_photon_map.bounds.min.x + us * volume_photon_map.bounds.extent.x;
    const double z = volume_photon_map.bounds.min.z + vs * volume_photon_map.bounds.extent.z;
    const double y = water_boundary_y(medium, x, z);
    const Vector3D p_surface(x, y, z);

    Vector3D d_air = p_surface - p_light;
    const double dist2 = d_air.norm2();
    if (dist2 <= 1e-12) continue;
    d_air = d_air.unit();

    const Vector3D n_air = water_boundary_normal(medium, x, z);
    const double cos_emit = std::max(0.0, dot(d_air, light->direction.unit()));
    const double cos_incident = std::max(0.0, -dot(n_air, d_air));
    if (cos_emit <= 0.0 || cos_incident <= 0.0) continue;

    Vector3D d_water;
    if (!refract_air_to_water(d_air, n_air, 1.33, &d_water)) continue;
    if (d_water.y >= -1e-4) continue;

    const double f0 = (1.0 - 1.33) / (1.0 + 1.33);
    const double fresnel = f0 * f0 +
        (1.0 - f0 * f0) * std::pow(1.0 - clamp01(cos_incident), 5.0);
    Vector3D power = light->radiance *
        (light->area * surface_area / photon_count) *
        (cos_emit * cos_incident * (1.0 - fresnel) / dist2);
    if (nearly_black(power)) continue;

    Ray photon_ray(p_surface + d_water * EPS_F, d_water);
    photon_ray.min_t = EPS_F;
    ++launched;

    for (int depth = 0; depth < max_depth; ++depth) {
      double t_enter, t_exit;
      if (!medium->clip_ray(photon_ray, t_enter, t_exit)) break;

      Intersection isect;
      const bool hit = bvh->intersect(photon_ray, &isect);
      const bool surface_in_medium =
          hit && isect.t > t_enter + EPS_F && isect.t < t_exit;
      const double segment_end = surface_in_medium ? isect.t : t_exit;
      if (segment_end <= t_enter) break;

      double scatter_t = INF_D;
      if (sigma_avg > 1e-8) {
        scatter_t = t_enter -
            std::log(std::max(1e-12, 1.0 - random_uniform())) / sigma_avg;
      }

      if (scatter_t < segment_end) {
        const Vector3D T = exp_transmittance(sigma_t, scatter_t - t_enter);
        const Vector3D scatter_power = power * T * albedo;
        if (nearly_black(scatter_power)) break;

        VolumetricPhotonMap::Photon photon;
        photon.position = photon_ray.o + photon_ray.d * scatter_t;
        photon.wi = -photon_ray.d;
        photon.power = scatter_power;
        insert_volume_photon(volume_photon_map, photon);

        Vector3D next_power = scatter_power;
        if (depth >= rr_start) {
          const double survive = std::max(0.05, std::min(0.95, luminance(next_power)));
          if (random_uniform() > survive) break;
          next_power /= survive;
        }

        const Vector3D next_dir = sample_hg_direction(photon_ray.d, medium->hg_g);
        photon_ray = Ray(photon.position + next_dir * EPS_F, next_dir);
        photon_ray.min_t = EPS_F;
        power = next_power;
        continue;
      }

      power = power * exp_transmittance(sigma_t, segment_end - t_enter);
      if (nearly_black(power) || !surface_in_medium) break;

      Matrix3x3 o2w;
      make_coord_space(o2w, isect.n);
      Matrix3x3 w2o = o2w.T();
      const Vector3D hit_p = photon_ray.o + photon_ray.d * isect.t;
      const Vector3D w_out = w2o * (-photon_ray.d);

      Vector3D wi;
      double pdf = 0.0;
      const Vector3D f = isect.bsdf->sample_f(w_out, &wi, &pdf);
      if (pdf <= 0.0 || f == Vector3D() || abs_cos_theta(wi) <= 0.0) break;

      Vector3D next_power = power * f * (abs_cos_theta(wi) / pdf);
      if (depth >= rr_start) {
        const double survive = std::max(0.05, std::min(0.95, luminance(next_power)));
        if (random_uniform() > survive) break;
        next_power /= survive;
      }
      if (nearly_black(next_power)) break;

      const Vector3D next_dir = (o2w * wi).unit();
      photon_ray = Ray(hit_p + next_dir * EPS_F, next_dir);
      photon_ray.min_t = EPS_F;
      power = next_power;
    }
  }

  if (!volume_photon_map.valid()) {
    volume_photon_map.clear();
    return;
  }

  fprintf(stdout,
          "[PathTracer] Built volume photon map: %dx%dx%d grid, %zu photons from %d launched paths, radius %.4f.\n",
          nx, ny, nz, volume_photon_map.photons.size(), launched,
          volume_photon_map.radius);
}

Vector3D PathTracer::estimate_vol_photon_lighting(const Ray& r,
                                                  double t_enter,
                                                  double t_exit) {
  Vector3D L_out;
  if (!medium || !volume_photon_map.valid() || t_exit <= t_enter) return L_out;

  const int n_samples = std::max(1, read_env_int("VPM_VOLUME_SAMPLES", 2));
  const double segment = t_exit - t_enter;
  const double pdf_t = 1.0 / segment;
  for (int i = 0; i < n_samples; ++i) {
    const double u = (i + random_uniform()) / n_samples;
    const double t = t_enter + u * segment;
    const Vector3D p = r.o + r.d * t;
    const Vector3D L_photons =
        gather_volume_photons(volume_photon_map, p, r.d, medium->hg_g);
    if (L_photons == Vector3D()) continue;

    const Vector3D T_cam = medium->det_transmittance(r, t_enter, t);
    L_out += (T_cam * L_photons) / pdf_t;
  }

  return L_out / n_samples;
}

Vector3D PathTracer::estimate_vol_direct_lighting(const Vector3D& p) {
  Vector3D L_out;
  for (SceneLight* light : scene->lights) {
    Vector3D wi_world;
    double dist_to_light, pdf;
    Vector3D radiance = light->sample_L(p, &wi_world, &dist_to_light, &pdf);
    if (pdf <= 0.0 || radiance == Vector3D()) continue;

    Ray shadow_ray(p, wi_world);
    shadow_ray.min_t = EPS_F;
    shadow_ray.max_t = dist_to_light - EPS_F;

    if (shadow_ray_blocked(shadow_ray)) continue;

    Vector3D tr = medium ? medium->ray_transmittance(shadow_ray, dist_to_light)
                         : Vector3D(1.0);
    L_out += tr * phase_isotropic() * radiance / pdf;
  }
  return L_out;
}

Vector3D PathTracer::zero_bounce_radiance(const Ray &r,
                                          const Intersection &isect) {
  // TODO: Part 3, Task 2
  // Returns the light that results from no bounces of light
  return isect.bsdf->get_emission();


}

Vector3D PathTracer::one_bounce_radiance(const Ray &r,
                                         const Intersection &isect) {
  // TODO: Part 3, Task 3
  // Returns either the direct illumination by hemisphere or importance sampling
  // depending on `direct_hemisphere_sample`
  if (direct_hemisphere_sample) {
    return estimate_direct_lighting_hemisphere(r, isect);
  }
  return estimate_direct_lighting_importance(r, isect);


}

Vector3D PathTracer::at_least_one_bounce_radiance(const Ray &r,
                                                  const Intersection &isect) {
  Matrix3x3 o2w;
  make_coord_space(o2w, isect.n);
  Matrix3x3 w2o = o2w.T();

  Vector3D hit_p = r.o + r.d * isect.t;
  Vector3D w_out = w2o * (-r.d);

  Vector3D L_out(0, 0, 0);

  if (!isect.bsdf->is_delta()) {
    L_out += one_bounce_radiance(r, isect);
  }

  if (r.depth >= max_ray_depth) {
    return L_out;
  }

  const int num_samples = isect.bsdf->is_delta()
                            ? std::max<size_t>(1, ns_refr)
                            : std::max<size_t>(1, ns_diff);
  Vector3D indirect;

  for (int i = 0; i < num_samples; ++i) {
    Vector3D wi;
    double pdf = 0.0;
    const Vector3D f = isect.bsdf->sample_f(w_out, &wi, &pdf);
    if (pdf <= 0.0 || f == Vector3D() || abs_cos_theta(wi) <= 0.0) {
      continue;
    }

    const Vector3D wi_world = o2w * wi;
    Ray next_ray(hit_p + wi_world * EPS_F, wi_world,
                 static_cast<int>(r.depth + 1));
    next_ray.min_t = EPS_F;

    indirect += f * est_radiance_global_illumination(next_ray) *
                (abs_cos_theta(wi) / pdf);
  }

  L_out += indirect / num_samples;

  return L_out;
}

Vector3D PathTracer::est_radiance_global_illumination(const Ray &r) {
  Vector3D L_out;

  if (medium) {
    // --- VOLUMETRIC PATH ---
    // Find the segment of this ray that is inside the medium [t_enter, t_exit]
    double t_enter, t_exit;
    if (!medium->clip_ray(r, t_enter, t_exit)) {
      // Ray misses bounded medium entirely — fall through to normal surface path
      goto surface_path;
    }

    {
      // Find nearest surface distance d
      Intersection isect;
      bool hit = bvh->intersect(r, &isect);
      double d = hit ? isect.t : INF_D;

      // Clamp the medium segment to not extend past the surface
      double seg_end = std::min(t_exit, d);
      if (seg_end <= t_enter) goto surface_path;

      // Volumetric single-scatter direct lighting via equiangular +
      // truncated-exp MIS (Kulla & Fajardo 2012). Integrates the
      // in-scattering term over the entire medium segment in one pass per
      // light — no delta tracking needed for the single-scatter estimator.
      Vector3D L_vol;
      if (read_env_int("VOLUME_DIRECT_ENABLE", 1) != 0) {
        L_vol += estimate_vol_direct_lighting_mis(r, t_enter, seg_end);
      }
      if (read_env_int("VPM_ENABLE", 1) != 0) {
        L_vol += estimate_vol_photon_lighting(r, t_enter, seg_end);
      }

      // Surface / environment contribution attenuated by chromatic
      // transmittance across the full medium segment. Depth-dependent color
      // shift (red attenuates faster than blue) emerges naturally from the
      // per-channel Beer-Lambert factor.
      Vector3D L_surf;
      if (hit) {
        Vector3D tr = medium->det_transmittance(r, t_enter, seg_end);
        L_surf = tr * (zero_bounce_radiance(r, isect) +
                       at_least_one_bounce_radiance(r, isect));
      } else if (envLight) {
        Vector3D tr = medium->det_transmittance(r, t_enter, seg_end);
        L_surf = tr * envLight->sample_dir(r);
      }

      return L_vol + L_surf;
    }
  }

  // --- SURFACE PATH (no medium, or ray missed bounded region) ---
  surface_path:
  {
  Intersection isect;
  if (!bvh->intersect(r, &isect))
    return envLight ? envLight->sample_dir(r) : L_out;

  L_out = zero_bounce_radiance(r, isect) +
          at_least_one_bounce_radiance(r, isect);
  return L_out;
  } // surface_path block
}

void PathTracer::raytrace_pixel(size_t x, size_t y) {
  // TODO (Part 1.2):
  // Make a loop that generates num_samples camera rays and traces them
  // through the scene. Return the average Vector3D.
  // You should call est_radiance_global_illumination in this function.

  // TODO (Part 5):
  // Modify your implementation to include adaptive sampling.
  // Use the command line parameters "samplesPerBatch" and "maxTolerance"
  int num_samples = ns_aa;          // total samples to evaluate
  Vector2D origin = Vector2D(x, y); // bottom left corner of the pixel
  Vector3D radiance;

  for (int i = 0; i < num_samples; ++i) {
    Vector2D sample = origin + gridSampler->get_sample();
    Ray ray = camera->generate_ray(sample.x / sampleBuffer.w,
                                   sample.y / sampleBuffer.h);
    radiance += est_radiance_global_illumination(ray);
  }

  sampleBuffer.update_pixel(radiance / num_samples, x, y);
  sampleCountBuffer[x + y * sampleBuffer.w] = num_samples;


}

void PathTracer::autofocus(Vector2D loc) {
  Ray r = camera->generate_ray(loc.x / sampleBuffer.w, loc.y / sampleBuffer.h);
  Intersection isect;

  bvh->intersect(r, &isect);

  camera->focalDistance = isect.t;
}

} // namespace CGL
