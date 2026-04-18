#pragma once

#include <cmath>
#include "scene/bbox.h"
#include "util/random_util.h"

namespace CGL {

struct Medium {
    double sigma_a;      // absorption at maximum density
    double sigma_s;      // scattering at maximum density
    double sigma_t;      // extinction at maximum density = sigma_a + sigma_s
    double albedo;       // sigma_s / sigma_t
    BBox*  bounds;       // nullptr = scene-wide; non-null = bounded region
    double fade_height;  // y-coordinate where fog fades to zero (0 = hard boundary)

    Medium(double sa, double ss, BBox* b = nullptr, double fh = 0.0)
        : sigma_a(sa), sigma_s(ss),
          sigma_t(sa + ss),
          albedo(ss / (sa + ss)),
          bounds(b),
          fade_height(fh) {}

    // --- Heterogeneous density ---

    // Local extinction at world position p.
    // Homogeneous if fade_height == 0; smoothstep falloff in y otherwise.
    double density(const Vector3D& p) const {
        if (fade_height <= 0.0) return sigma_t;
        // smoothstep: full density at y=0, zero at y=fade_height
        double t = std::max(0.0, std::min(1.0, p.y / fade_height));
        double s = t * t * (3.0 - 2.0 * t);   // smoothstep 0→1
        return sigma_t * (1.0 - s);
    }

    double density_albedo(const Vector3D& p) const {
        // albedo stays constant, only total density varies
        return albedo;
    }

    // --- Ray segment clipping ---

    bool clip_ray(const Ray& r, double& t_enter, double& t_exit) const {
        if (!bounds) {
            t_enter = r.min_t;
            t_exit  = INF_D;
            return true;
        }
        double t0 = 0.0, t1 = INF_D;
        if (!bounds->intersect(r, t0, t1)) return false;
        t_enter = std::max(t0, r.min_t);
        t_exit  = t1;
        return t_enter < t_exit;
    }

    // --- Free-path sampling via delta tracking ---
    // Works for both homogeneous and heterogeneous density.
    // Returns distance from t_start to scatter event, or INF_D if ray escapes to t_end.
    double delta_track(const Ray& r, double t_start, double t_end) const {
        if (fade_height <= 0.0) {
            // Homogeneous: simple inverse CDF, much faster
            double s = -std::log(1.0 - random_uniform()) / sigma_t;
            return (t_start + s < t_end) ? t_start + s : INF_D;
        }
        // Heterogeneous: null-collision (Woodcock) tracking
        // sigma_t is the majorant (max density across the volume)
        double t = t_start;
        while (true) {
            t -= std::log(1.0 - random_uniform()) / sigma_t;  // sample with majorant
            if (t >= t_end) return INF_D;
            Vector3D p = r.o + t * r.d;
            double local = density(p);
            if (random_uniform() < local / sigma_t) return t;  // real collision
            // else: null collision, continue
        }
    }

    // --- Deterministic transmittance via numerical quadrature ---
    // Uses midpoint-rule integration — no random samples, so zero variance.
    // 32 steps is accurate for smooth density functions like smoothstep.
    double det_transmittance(const Ray& r, double t_start, double t_end) const {
        if (fade_height <= 0.0) {
            // Homogeneous: exact analytic result
            double seg = t_end - t_start;
            return (seg <= 0.0) ? 1.0 : std::exp(-sigma_t * seg);
        }
        const int N = 32;
        double dt = (t_end - t_start) / N;
        double tau = 0.0;
        for (int i = 0; i < N; i++) {
            double t_mid = t_start + (i + 0.5) * dt;
            tau += density(r.o + t_mid * r.d) * dt;
        }
        return std::exp(-tau);
    }

    // Transmittance for a shadow ray — clips to bounds, then integrates deterministically
    double ray_transmittance(const Ray& r, double dist) const {
        double t_enter, t_exit;
        if (!clip_ray(r, t_enter, t_exit)) return 1.0;
        double seg_end = std::min(t_exit, dist);
        if (seg_end <= t_enter) return 1.0;
        return det_transmittance(r, t_enter, seg_end);
    }
};

// Isotropic phase function: uniform over full sphere
// eval/pdf = (1/4π)/(1/4π) = 1 when direction sampled from phase fn
inline double phase_isotropic() {
    return 1.0 / (4.0 * M_PI);
}

} // namespace CGL
