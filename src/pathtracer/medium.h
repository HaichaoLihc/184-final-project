#pragma once

#include <cmath>
#include <cstdlib>
#include <limits>

#include "scene/bbox.h"
#include "util/random_util.h"

namespace CGL {

// Participating medium with wavelength-dependent (chromatic) absorption
// (sigma_a) and scattering (sigma_s) coefficients. Optional heterogeneous
// density via a height-based smoothstep falloff, and optional bounded region
// via a BBox. Delta tracking is used for free-flight sampling; transmittance
// is evaluated per channel.
//
// Environment-variable overrides (read in the constructor):
//   MEDIUM_SIGMA_A, MEDIUM_SIGMA_S                 -- scalar (all channels)
//   MEDIUM_SIGMA_A_R/G/B, MEDIUM_SIGMA_S_R/G/B     -- per-channel override
//   MEDIUM_MAX_DIST                                -- shadow-ray cap for
//                                                     directional / infinite
//                                                     lights (default +inf)
struct Medium {
    Vector3D sigma_a;
    Vector3D sigma_s;
    BBox*    bounds;        ///< nullptr = scene-wide
    double   fade_height;   ///< 0 = homogeneous; otherwise smoothstep in y
    double   max_shadow_dist; ///< cap for directional/infinite-light shadow rays

    // Scalar constructor kept for backward compatibility. Env vars override.
    Medium(double sa, double ss, BBox* b = nullptr, double fh = 0.0)
        : sigma_a(sa), sigma_s(ss),
          bounds(b),
          fade_height(fh),
          max_shadow_dist(std::numeric_limits<double>::infinity()) {
        override_from_env();
    }

    // --- Coefficient accessors ---

    Vector3D sigma_t() const { return sigma_a + sigma_s; }
    double sigma_t_avg() const {
        Vector3D st = sigma_t();
        return (st.x + st.y + st.z) / 3.0;
    }
    // Chromatic single-scattering albedo (per channel).
    Vector3D albedo_c() const {
        Vector3D st = sigma_t();
        return Vector3D(st.x > 1e-8 ? sigma_s.x / st.x : 0.0,
                        st.y > 1e-8 ? sigma_s.y / st.y : 0.0,
                        st.z > 1e-8 ? sigma_s.z / st.z : 0.0);
    }

    // --- Heterogeneous density multiplier in [0,1] ---
    // Homogeneous (=1) if fade_height == 0; smoothstep falloff in y otherwise.
    double density_scale(const Vector3D& p) const {
        if (fade_height <= 0.0) return 1.0;
        double t = std::max(0.0, std::min(1.0, p.y / fade_height));
        double s = t * t * (3.0 - 2.0 * t);  // smoothstep 0->1
        return 1.0 - s;
    }

    // --- Ray segment clipping against the medium bounds ---
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

    // --- Free-path sampling via delta tracking (scalar majorant) ---
    // For chromatic media, distance sampling uses sigma_t_avg as majorant.
    // Returns absolute t in [t_start, t_end) for a real collision, or INF_D
    // if the ray escapes the segment.
    double delta_track(const Ray& r, double t_start, double t_end) const {
        double sigma_maj = sigma_t_avg();
        if (sigma_maj <= 1e-8) return INF_D;
        if (fade_height <= 0.0) {
            double s = -std::log(1.0 - random_uniform()) / sigma_maj;
            return (t_start + s < t_end) ? t_start + s : INF_D;
        }
        // Woodcock / null-collision tracking.
        double t = t_start;
        while (true) {
            t -= std::log(1.0 - random_uniform()) / sigma_maj;
            if (t >= t_end) return INF_D;
            Vector3D p = r.o + t * r.d;
            double local = density_scale(p);  // in [0, 1]
            if (random_uniform() < local) return t;
        }
    }

    // --- Chromatic transmittance along a segment [t_start, t_end] ---
    // Homogeneous: closed-form three-channel Beer-Lambert.
    // Heterogeneous: midpoint-rule quadrature of scalar optical depth
    // integrated with chromatic sigma_t (zero-variance, 32 steps).
    Vector3D det_transmittance(const Ray& r, double t_start, double t_end) const {
        if (t_end <= t_start) return Vector3D(1.0);
        Vector3D st = sigma_t();
        if (fade_height <= 0.0) {
            double seg = t_end - t_start;
            return Vector3D(std::exp(-st.x * seg),
                            std::exp(-st.y * seg),
                            std::exp(-st.z * seg));
        }
        const int N = 32;
        double dt = (t_end - t_start) / N;
        double tau_scalar = 0.0;
        for (int i = 0; i < N; ++i) {
            double t_mid = t_start + (i + 0.5) * dt;
            tau_scalar += density_scale(r.o + t_mid * r.d) * dt;
        }
        return Vector3D(std::exp(-st.x * tau_scalar),
                        std::exp(-st.y * tau_scalar),
                        std::exp(-st.z * tau_scalar));
    }

    // Shadow-ray transmittance: clipped to bounds and capped by
    // max_shadow_dist so directional / env lights still contribute.
    Vector3D ray_transmittance(const Ray& r, double dist) const {
        double t_enter, t_exit;
        if (!clip_ray(r, t_enter, t_exit)) return Vector3D(1.0);
        double seg_end = std::min(t_exit, dist);
        if (seg_end <= t_enter) return Vector3D(1.0);
        seg_end = std::min(seg_end, t_enter + max_shadow_dist);
        return det_transmittance(r, t_enter, seg_end);
    }

    // --- Environment-variable overrides ---
    void override_from_env() {
        auto rd = [](const char* k, double def) {
            const char* v = std::getenv(k);
            return v ? std::atof(v) : def;
        };
        double a_scalar = rd("MEDIUM_SIGMA_A", -1.0);
        double s_scalar = rd("MEDIUM_SIGMA_S", -1.0);
        if (a_scalar >= 0.0) sigma_a = Vector3D(a_scalar);
        if (s_scalar >= 0.0) sigma_s = Vector3D(s_scalar);
        sigma_a.x = rd("MEDIUM_SIGMA_A_R", sigma_a.x);
        sigma_a.y = rd("MEDIUM_SIGMA_A_G", sigma_a.y);
        sigma_a.z = rd("MEDIUM_SIGMA_A_B", sigma_a.z);
        sigma_s.x = rd("MEDIUM_SIGMA_S_R", sigma_s.x);
        sigma_s.y = rd("MEDIUM_SIGMA_S_G", sigma_s.y);
        sigma_s.z = rd("MEDIUM_SIGMA_S_B", sigma_s.z);
        max_shadow_dist = rd("MEDIUM_MAX_DIST", max_shadow_dist);
    }
};

// Isotropic phase function: p(w, w') = 1/(4π).
inline double phase_isotropic() {
    return 1.0 / (4.0 * M_PI);
}

} // namespace CGL
