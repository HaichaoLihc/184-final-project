#include "pathtracer.h"

#include "scene/light.h"
#include "scene/sphere.h"
#include "scene/triangle.h"
#include "pathtracer/sampler.h"
#include "util/random_util.h"


using namespace CGL::SceneObjects;

namespace CGL {

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
  sampleBuffer.clear();
  sampleCountBuffer.clear();
  sampleBuffer.resize(0, 0);
  sampleCountBuffer.resize(0, 0);
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
    if (!bvh->intersect(shadow_ray, &light_isect)) {
      continue;
    }

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

      Intersection blocker;
      if (bvh->intersect(shadow_ray, &blocker)) {
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

    Intersection blocker;
    if (bvh->intersect(shadow_ray, &blocker)) continue;

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

  // TODO: Part 4, Task 2
  // Returns the one bounce radiance + radiance from extra bounces at this point.
  // Should be called recursively to simulate extra bounces.


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

      // Delta tracking: returns absolute t along ray, or INF_D if escaped to seg_end
      double t_scatter = medium->delta_track(r, t_enter, seg_end);

      if (t_scatter < INF_D) {
        // MEDIUM SCATTER EVENT
        // Weight: albedo (T_r*sigma_s / pdf cancels via delta tracking acceptance)
        Vector3D scatter_p = r.o + t_scatter * r.d;

        UniformSphereSampler3D sphere_sampler;
        Vector3D new_dir = sphere_sampler.get_sample();

        // Single-scatter approximation: direct lighting only at each scatter point.
        // The recursive indirect term (random direction bounce) is the dominant
        // noise source — dropping it eliminates graininess at minimal visual cost
        // for fog lit from above where inter-surface bounces through the fog are subtle.
        // Chromatic albedo weight: sigma_s_c / sigma_t_c (per channel).
        L_out = medium->albedo_c() * estimate_vol_direct_lighting(scatter_p);
        return L_out;

      } else {
        // SURFACE EVENT — attenuate by chromatic transmittance across the
        // medium segment. This gives rise to depth-dependent color shift
        // (red attenuates faster than blue in an underwater medium).
        if (!hit) return envLight ? envLight->sample_dir(r) : L_out;
        Vector3D tr = medium->det_transmittance(r, t_enter, seg_end);
        L_out = tr * (zero_bounce_radiance(r, isect) + one_bounce_radiance(r, isect));
        return L_out;
      }
    }
  }

  // --- SURFACE PATH (no medium, or ray missed bounded region) ---
  surface_path:
  {
  Intersection isect;
  if (!bvh->intersect(r, &isect))
    return envLight ? envLight->sample_dir(r) : L_out;

  L_out = zero_bounce_radiance(r, isect) + one_bounce_radiance(r, isect);
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
