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

    // Wavy top-face perturbation. The top face of the medium BBox is at
    // y = bounds->max.y; with boundary_amp > 0 we instead treat it as
    //   y_top(x, z) = bounds->max.y + boundary_amp * h(x, z)
    // where h is a sum of sinusoids. This produces (x, z)-dependent path
    // lengths through the medium for both camera rays entering from above
    // and shadow rays exiting upward — i.e. the visual signature of "god
    // rays under a wavy water surface". No geometry change; only the
    // analytic ray-segment endpoints in clip_ray() shift.
    double   boundary_amp;
    double   boundary_freq;

    // Henyey-Greenstein phase-function asymmetry parameter g in (-1, 1).
    //   g = 0       -> isotropic (default)
    //   g > 0       -> forward-peaked (ocean water typical g ~ 0.85)
    //   g < 0       -> backward-peaked
    // Anisotropic forward scattering is what makes single-scatter shafts /
    // god rays actually visible in water and fog: the in-scatter integrand
    // becomes sharply peaked when the camera ray, scatter point, and light
    // are roughly collinear. With isotropic scatter no shaft is ever
    // distinguishable from background — the volume just looks like flat fog.
    double   hg_g;

    // Scalar constructor kept for backward compatibility. Env vars override.
    Medium(double sa, double ss, BBox* b = nullptr, double fh = 0.0)
        : sigma_a(sa), sigma_s(ss),
          bounds(b),
          fade_height(fh),
          max_shadow_dist(std::numeric_limits<double>::infinity()),
          boundary_amp(0.0),
          boundary_freq(8.0),
          hg_g(0.0) {
        override_from_env();
    }

    // Sum-of-sinusoids height field h(x, z) for the wavy top face. Range
    // roughly in [-1.5, 1.5]; multiplied by boundary_amp at the call site.
    double boundary_height(double x, double z) const {
        const double k1 = boundary_freq;
        const double k2 = boundary_freq * 1.7;
        return std::sin(k1 * x + 0.31) * std::cos(k1 * z + 1.27)
             + 0.5 * std::sin(k2 * (x + z) + 2.11);
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
    // When boundary_amp > 0, whichever endpoint corresponds to the top face
    // (y ≈ bounds->max.y) is shifted by amp * h(x, z) / r.d.y, which is the
    // first-order solution of r.o.y + t * r.d.y = ymax + amp * h(x, z) at
    // the unperturbed (x, z). Accurate to O(amp^2) for small amplitudes.
    bool clip_ray(const Ray& r, double& t_enter, double& t_exit) const {
        if (!bounds) {
            t_enter = r.min_t;
            t_exit  = INF_D;
            return true;
        }
        double t0 = 0.0, t1 = INF_D;
        if (!bounds->intersect(r, t0, t1)) return false;

        if (boundary_amp > 0.0 && std::abs(r.d.y) > 1e-6) {
            const double ymax = bounds->max.y;
            for (double* tp : {&t0, &t1}) {
                const double y_at = r.o.y + (*tp) * r.d.y;
                if (std::abs(y_at - ymax) < 1e-3) {
                    const double x = r.o.x + (*tp) * r.d.x;
                    const double z = r.o.z + (*tp) * r.d.z;
                    *tp = (ymax + boundary_amp * boundary_height(x, z) - r.o.y) / r.d.y;
                }
            }
            if (t0 > t1) std::swap(t0, t1);
        }

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
        boundary_amp    = rd("MEDIUM_BOUNDARY_AMP",  boundary_amp);
        boundary_freq   = rd("MEDIUM_BOUNDARY_FREQ", boundary_freq);
        hg_g            = rd("MEDIUM_HG_G",          hg_g);
    }
};

// Isotropic phase function: p(w, w') = 1/(4π).
inline double phase_isotropic() {
    return 1.0 / (4.0 * M_PI);
}

// Henyey-Greenstein phase function p(cos θ; g) for cos θ = dot(ω_in, ω_out).
// Reduces to isotropic when |g| is tiny. g in (-1, 1).
inline double phase_hg(double cos_theta, double g) {
    if (std::abs(g) < 1e-4) return 1.0 / (4.0 * M_PI);
    const double denom = 1.0 + g * g - 2.0 * g * cos_theta;
    return (1.0 - g * g) / (4.0 * M_PI * denom * std::sqrt(std::max(denom, 1e-12)));
}

// ---------------------------------------------------------------------------
// Equiangular sampling along a ray segment towards a point light.
//
// References:
//   Kulla & Fajardo, "Importance Sampling Techniques for Path Tracing in
//   Participating Media", EGSR 2012.
//
// Given a camera ray (o, d), a point light at p_L, and a ray-segment
// [t_near, t_far], equiangular sampling draws t* with density concentrated
// near the light — the region where the single-scatter integrand is sharp.
//
//   t_star = dot(p_L - o, d)               (closest-point param on ray)
//   D      = |p_L - (o + t_star * d)|      (perpendicular distance)
//   theta_a = atan((t_near - t_star) / D)
//   theta_b = atan((t_far  - t_star) / D)
//   theta  = lerp(theta_a, theta_b, u)
//   t      = t_star + D * tan(theta)
//   pdf(t) = D / ((D^2 + (t - t_star)^2) * (theta_b - theta_a))
//
// Returns false if the geometry degenerates (D ≈ 0 or empty interval); in
// that case the caller should fall back to distance sampling only.
struct EquiangularSampler {
    double t_star;
    double D;
    double theta_a;
    double theta_b;
    double dtheta;
    bool valid;

    EquiangularSampler(const Vector3D& o, const Vector3D& d,
                       const Vector3D& p_light,
                       double t_near, double t_far) {
        t_star = dot(p_light - o, d);
        Vector3D q = o + d * t_star;
        D = (p_light - q).norm();
        if (D < 1e-4 || t_far <= t_near) {
            valid = false;
            theta_a = theta_b = dtheta = 0.0;
            return;
        }
        theta_a = std::atan((t_near - t_star) / D);
        theta_b = std::atan((t_far  - t_star) / D);
        dtheta  = theta_b - theta_a;
        valid   = dtheta > 1e-6;
    }

    // Sample a distance t along the ray given uniform u in [0,1].
    double sample(double u, double* pdf) const {
        double theta = theta_a + u * dtheta;
        double t = t_star + D * std::tan(theta);
        if (pdf) *pdf = pdf_at(t);
        return t;
    }

    // Evaluate the pdf at an arbitrary t (for MIS weighting).
    double pdf_at(double t) const {
        if (!valid) return 0.0;
        double dt = t - t_star;
        return D / ((D * D + dt * dt) * dtheta);
    }
};

// Truncated-exponential distance sampling over [t_near, t_far] with
// parameter sigma. pdf is normalized over the interval so that it and
// equiangular pdfs can be combined under MIS on the same domain.
inline double truncexp_sample(double sigma, double t_near, double t_far,
                              double u, double* pdf) {
    double seg = t_far - t_near;
    double denom = 1.0 - std::exp(-sigma * seg);
    if (denom < 1e-12) {
        if (pdf) *pdf = 1.0 / std::max(seg, 1e-12);
        return t_near + u * seg;
    }
    double t = t_near - std::log(1.0 - u * denom) / sigma;
    if (pdf) *pdf = sigma * std::exp(-sigma * (t - t_near)) / denom;
    return t;
}

inline double truncexp_pdf(double sigma, double t_near, double t_far, double t) {
    if (t < t_near || t > t_far) return 0.0;
    double seg = t_far - t_near;
    double denom = 1.0 - std::exp(-sigma * seg);
    if (denom < 1e-12) return 1.0 / std::max(seg, 1e-12);
    return sigma * std::exp(-sigma * (t - t_near)) / denom;
}

// Power heuristic (beta = 2) for two-strategy MIS.
inline double mis_power2(double pdf_a, double pdf_b) {
    double a2 = pdf_a * pdf_a;
    double b2 = pdf_b * pdf_b;
    double s = a2 + b2;
    return s > 0.0 ? a2 / s : 0.0;
}

} // namespace CGL
