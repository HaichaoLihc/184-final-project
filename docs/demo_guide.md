# Underwater Volumetric Path Tracer — Demo Guide

A field manual for the demo. Every section is organised so you can jump
straight to a topic and find: **what the feature does**, **the math**, **the
file:line where it lives**, **how to toggle it**, and **the tricky questions
a TA might drill on**.

---

## 0 · Project at a glance

| Layer | What was added on top of vanilla CS184 path tracer |
|-------|----------------------------------------------------|
| Surface BSDF | `BSDF::reflect`, `BSDF::refract`, `MirrorBSDF`, `RefractionBSDF`, `GlassBSDF` (full dielectric Fresnel + probabilistic Russian-roulette branch between reflect/refract lobes, with TIR fallback) |
| Integrator | Recursive `at_least_one_bounce_radiance` with per-bounce indirect samples, hard `max_ray_depth` cap, separate sample counts for diffuse vs. delta surfaces |
| Light transport through glass | `shadow_ray_blocked` walks shadow rays straight through up to 16 transmissive surfaces (transparent-shadow approximation) |
| Participating medium | Per-channel `σ_a`, `σ_s` chromatic Beer-Lambert in `Medium::det_transmittance`; bounded scene-region BBox; optional smoothstep heterogeneous density |
| Volumetric direct light | Single-scatter in-scatter integrand sampled by **equiangular + truncated-exponential MIS** (Kulla & Fajardo 2012 / power heuristic β=2) |
| Phase function | **Henyey-Greenstein**, `g` configurable per-medium |
| Water surface (Plan A) | Wavy *medium-boundary* perturbation: top BBox face displaced by analytic `h(x,z)` so ray-segment endpoints become spatially varying |
| Water surface (Plan B) | Wavy *shading-normal* perturbation in `Triangle::intersect` for upward-facing GlassBSDF triangles (env-var gated, no geometry change) |
| Scenes | `dae/sky/CBspheres_lambertian.dae`, `dae/sky/CBbunny_water.dae`, `dae/sky/CBspheres_underwater.dae` |

The whole pipeline is **unbiased Monte Carlo path tracing** + a single
biased shortcut (transparent shadows). Everything else has a defensible
estimator.

---

## 1 · Cheat-sheet: where everything lives

| Feature | File | Symbol / line |
|---------|------|---------------|
| Reflect about local Z | [advanced_bsdf.cpp](../src/pathtracer/advanced_bsdf.cpp) | `BSDF::reflect`, line 175 |
| Snell + TIR | [advanced_bsdf.cpp](../src/pathtracer/advanced_bsdf.cpp) | `BSDF::refract`, line 180 |
| Dielectric Fresnel | [advanced_bsdf.cpp](../src/pathtracer/advanced_bsdf.cpp) | `dielectric_fresnel`, line 18 |
| Mirror sample | [advanced_bsdf.cpp](../src/pathtracer/advanced_bsdf.cpp) | `MirrorBSDF::sample_f`, line 41 |
| Pure refraction | [advanced_bsdf.cpp](../src/pathtracer/advanced_bsdf.cpp) | `RefractionBSDF::sample_f`, line 118 |
| Glass (Fresnel-weighted reflect/refract) | [advanced_bsdf.cpp](../src/pathtracer/advanced_bsdf.cpp) | `GlassBSDF::sample_f`, line 145 |
| Transparent shadow walk | [pathtracer.cpp](../src/pathtracer/pathtracer.cpp) | `shadow_ray_blocked`, line 54 |
| NEE direct (importance) | [pathtracer.cpp](../src/pathtracer/pathtracer.cpp) | `estimate_direct_lighting_importance`, line 152 |
| NEE direct (hemisphere) | [pathtracer.cpp](../src/pathtracer/pathtracer.cpp) | `estimate_direct_lighting_hemisphere`, line 84 |
| Volumetric MIS direct | [pathtracer.cpp](../src/pathtracer/pathtracer.cpp) | `estimate_vol_direct_lighting_mis`, line 227 |
| Recursive indirect | [pathtracer.cpp](../src/pathtracer/pathtracer.cpp) | `at_least_one_bounce_radiance`, line 402 |
| Top-level integrator | [pathtracer.cpp](../src/pathtracer/pathtracer.cpp) | `est_radiance_global_illumination`, line 448 |
| Medium config (defaults) | [raytraced_renderer.cpp](../src/pathtracer/raytraced_renderer.cpp) | line 73 onward |
| Medium core | [medium.h](../src/pathtracer/medium.h) | `struct Medium`, line 24 |
| Beer-Lambert | [medium.h](../src/pathtracer/medium.h) | `det_transmittance`, line 104 |
| Wavy boundary clip | [medium.h](../src/pathtracer/medium.h) | `clip_ray`, line 65 |
| HG phase | [medium.h](../src/pathtracer/medium.h) | `phase_hg`, line 218 |
| Equiangular sampler | [medium.h](../src/pathtracer/medium.h) | `EquiangularSampler`, line 207 |
| Truncated-exp sampler | [medium.h](../src/pathtracer/medium.h) | `truncexp_sample`, line 289 |
| MIS power heuristic | [medium.h](../src/pathtracer/medium.h) | `mis_power2`, line 311 |
| Shading-normal water perturb | [triangle.cpp](../src/scene/triangle.cpp) | `perturb_water_shading_normal`, line 28 |

---

## 2 · Ray lifecycle (what happens for ONE underwater pixel)

```
camera.generate_ray
        │
        ▼
est_radiance_global_illumination(r)
        │
        ├── medium && clip_ray(r, t_enter, t_exit)
        │       └── (a) L_vol = estimate_vol_direct_lighting_mis(r, t_enter, seg_end)
        │              · for each light:
        │                  · equiangular sample t_eq + truncexp sample t_dt
        │                  · MIS combine w/ power heuristic (β = 2)
        │                  · phase_f = phase_hg(dot(wi, r.d), g)
        │                  · shadow_ray_blocked walks through glass surfaces
        │                  · contrib = T_cam · σ_s · phase · T_sh · L_i / pdf
        │       └── (b) L_surf = T_cam · (zero_bounce + at_least_one_bounce)
        │
        ▼
at_least_one_bounce_radiance(r, isect)
        │
        ├── if !is_delta:   L += one_bounce_radiance (NEE direct)
        ├── if depth ≥ max: return
        ├── for ns_diff or ns_refr samples:
        │       · f = bsdf->sample_f(wo, wi, pdf)        ← GlassBSDF picks reflect or refract via Fresnel
        │       · spawn ray (depth+1, EPS_F offset)
        │       · indirect += f · est_radiance_global_illumination(next) · cos / pdf
        │       (this recursion is what carries light THROUGH the water surface)
        └── L += indirect / num_samples
```

Two non-obvious points worth memorising:

1. **Direct lighting at the surface goes through `shadow_ray_blocked`, which
   treats glass as transparent**. Fast and clean, but biased — it does not
   refract the shadow ray, so caustic focusing on the floor is missing.
2. **Indirect lighting through the water uses the real `GlassBSDF`**:
   reflection/refraction probability comes from a per-sample dielectric
   Fresnel coefficient, not Schlick.

---

## 3 · Per-feature deep dive

### 3.1 BSDF::reflect (`advanced_bsdf.cpp:175`)

Local-frame mirror reflection. With surface normal aligned to z:

$$\omega_i = (-\omega_{o,x},\,-\omega_{o,y},\,\omega_{o,z})$$

Cost: 3 sign flips. Used by Mirror, Glass, Refraction (TIR fallback).

**TA might ask** *"why is z preserved and x,y flipped?"* — because reflection
about the plane `z = 0` keeps the normal component (along z) unchanged and
flips the tangential components.

### 3.2 BSDF::refract (`advanced_bsdf.cpp:180`)

Snell's law with full TIR detection. Sign convention: `wo.z > 0` means we're
exiting the surface back toward the *incident* medium, so the ray is entering
through vacuum (η = 1/ior); `wo.z < 0` means we're already inside and going
out (η = ior).

```
sin²θ_t = η² · (1 − cos²θ_i)
if sin²θ_t ≥ 1   → TIR, return false
cos θ_t  = √(1 − sin²θ_t)
ω_i      = (−η ω_o,x,  −η ω_o,y,  ±cos θ_t)   sign opposite to wo.z
```

**TA might ask** *"what's the critical angle for water?"* —
`asin(1/1.33) ≈ 48.6°`. Past that, total internal reflection.

**TA might ask** *"what about the η²ₜ/η²ᵢ radiance scaling?"* — In a strict
unbiased estimator you scale transmitted radiance by `(η_t / η_i)²` because
solid-angle compresses across the interface. We don't apply it in our
`RefractionBSDF::sample_f` / `GlassBSDF::sample_f`; the trade-off is a small
multiplicative bias on transmitted radiance. PBRT calls this the
"non-symmetric scattering correction" — see PBRT 9.5.2.

### 3.3 dielectric_fresnel (`advanced_bsdf.cpp:18`)

Full unpolarised Fresnel reflectance — the *exact* formula, not Schlick's
approximation:

$$
r_\parallel = \frac{\eta_t \cos\theta_i - \eta_i \cos\theta_t}
                   {\eta_t \cos\theta_i + \eta_i \cos\theta_t}
\qquad
r_\perp = \frac{\eta_i \cos\theta_i - \eta_t \cos\theta_t}
                {\eta_i \cos\theta_i + \eta_t \cos\theta_t}
$$

$$F_r = \tfrac{1}{2}\left(r_\parallel^2 + r_\perp^2\right)$$

At normal incidence on water (η_i = 1, η_t = 1.33):

$$F_r = \left(\frac{1.33-1}{1.33+1}\right)^2 \approx 0.0204$$

So at zero angle only ~2 % of light reflects, ~98 % refracts. At grazing
angle `F_r → 1` — the reason a still pond looks mirror-like at low angles.

**TA might ask** *"why exact instead of Schlick?"* — Schlick
`R₀ + (1−R₀)(1−cos θ)⁵` is an approximation that errs at grazing for low IORs.
We use the exact formula because it's < 10 lines and removes a source of
error during demo / parameter sweeps.

### 3.4 GlassBSDF::sample_f (`advanced_bsdf.cpp:145`)

Russian-roulette over the two delta lobes:

```
fr = dielectric_fresnel(...)
if !can_refract        →  reflect, pdf = 1, return reflectance / |cos θ|
if coin_flip(fr)       →  reflect, pdf = fr,   return reflectance · fr / |cos θ|
else                   →  refract, pdf = 1−fr, return transmittance · (1−fr) / |cos θ|
```

Returning `value · pdf / |cos θ|` exploits the integrator's `f · cos / pdf`
weighting so the final radiance contribution comes out exactly `reflectance`
or `transmittance` — energy-conserving up to the Fresnel split.

**TA might ask** *"why pick one lobe instead of summing both?"* — Russian
roulette over delta lobes keeps a single branch per sample. With enough
samples the expectation is identical to evaluating both, but the path
tracer's branching depth is now a normal random variable rather than a
geometric series of branchings.

**TA might ask** *"is this a delta BSDF? what does that mean for NEE?"* — Yes,
both lobes are Diracs (perfect specular). For delta surfaces we **skip** NEE
direct lighting (`at_least_one_bounce_radiance` line 413) because the
probability that a uniformly-sampled light direction hits the delta lobe is
zero. Delta surfaces only get illumination via the recursive indirect ray.

### 3.5 shadow_ray_blocked (`pathtracer.cpp:54`)

The single biased shortcut in the whole pipeline.

```
remaining = r.max_t
loop up to 16 times:
    if BVH miss along [min_t, remaining] → return false (light visible)
    if hit surface is GlassBSDF or RefractionBSDF:
        skip past it (advance origin by t + EPS_F), continue
    else:
        return true (opaque blocker)
return true
```

**Why we did this**: without it, NEE shadow rays from an underwater scatter
point or surface point to the ceiling light hit the water surface (a glass
BVH primitive) and are killed — every underwater pixel goes black except the
slow, noisy contribution from indirect bounces. Convergence becomes
hopeless.

**Why it's biased**:

- The shadow ray is *not* refracted at the interface, so the apparent light
  position from below water is unbent.
- The Fresnel transmittance factor `1 − F_r` is *not* multiplied into `L`,
  so transmissive surfaces add no attenuation to direct lighting.
- The shadow ray does not generate caustics. Caustics are by definition the
  spatial focusing of refracted light onto a receiver; if shadow rays don't
  refract, there is nothing to focus.

The trade is "no caustics" for "underwater scenes converge in 64–256 spp
instead of 4096+ spp."

**TA might ask** *"why not refract the shadow ray?"* — refracting splits the
shadow ray into a small number of rays (or one ray with a bent target),
which loses the constant-time `O(1)` path between scatter and light.
Production renderers handle this with photon mapping or manifold next-event
estimation; both are out of scope for this project.

**TA might ask** *"why a 16-skip cap?"* — defensive: if a scene has many
nested transmissive surfaces, we don't want an infinite loop. 16 is plenty
for any realistic scene.

### 3.6 estimate_vol_direct_lighting_mis (`pathtracer.cpp:227`)

Single-scatter direct lighting in a participating medium, sampled by MIS
between two strategies on the same ray segment.

The integrand for one light along the camera ray's medium segment
`[t_enter, t_exit]`:

$$
L_{\mathrm{vol}}^{(\ell)}=\int_{t_e}^{t_x}\!T_{\mathrm{cam}}(t)\,\sigma_s\,
p(\omega,\omega')\,T_{\mathrm{sh}}(t)\,\frac{L_i(t)}{\mathrm{pdf}_\ell}\,dt
$$

Two estimators:

- **Equiangular** (Kulla & Fajardo 2012). Density concentrated near the
  closest-point projection of the light onto the ray; excellent for finite
  point/area lights.

  $$\mathrm{pdf}_{\mathrm{eq}}(t)=\frac{D}{(D^2+(t-t^\star)^2)\,\Delta\theta}$$

  where `t* = dot(p_L − r.o, r.d)` is the projection parameter and `D` is
  the perpendicular distance from light to ray.

- **Truncated exponential**. Density follows Beer-Lambert falloff so it's
  good for "near the camera" scatter where transmittance is high.

  $$\mathrm{pdf}_{\mathrm{dt}}(t)=\frac{\sigma\,e^{-\sigma(t-t_e)}}{1-e^{-\sigma(t_x-t_e)}}$$

Combined with the **power heuristic with β = 2**:

$$w_a(t)=\frac{\mathrm{pdf}_a(t)^2}{\mathrm{pdf}_a(t)^2+\mathrm{pdf}_b(t)^2}$$

**TA might ask** *"why MIS, not just one strategy?"* — Equiangular is great
near the light but its pdf collapses far from the light; truncexp is great
near the camera but undersamples the light's neighbourhood. MIS uses each
where it's strong and falls back to the other where it's weak. The variance
reduction over either alone is dramatic for grazing geometry.

**TA might ask** *"directional / infinite lights?"* — They have no finite
`p_L`, so equiangular can't be set up. Code falls through to truncexp alone
(`pathtracer.cpp:280`).

### 3.7 phase_hg (`medium.h:218`)

Henyey-Greenstein phase function, `g ∈ (−1, 1)`:

$$p(\cos\theta;g)=\frac{1}{4\pi}\cdot\frac{1-g^2}{(1+g^2-2g\cos\theta)^{3/2}}$$

- `g = 0` → isotropic 1/(4π)
- `g > 0` → forward-peaked. **Ocean water uses g ≈ 0.85**; project default 0.75.
- `g < 0` → back-peaked.

cosθ here is `dot(ω_to_light, ω_camera_ray)` — i.e., the angle between
"how the light arrives at the scatter point" and "where the camera was
looking when the ray reached the scatter point". Forward-peaked phase makes
visible god-ray shafts possible. With isotropic phase, no shaft is ever
distinguishable from background regardless of σ_s.

**TA might ask** *"why HG and not e.g. Rayleigh, Mie, double-HG?"* —
HG is the standard cheap model for ocean water, peaking strongly forward.
Mie / double-HG model fine wavelength-dependent backscatter peaks; for our
project the single-parameter HG is sufficient and the math/sampling is
tractable.

### 3.8 Wavy medium boundary (`medium.h:65`)

Plan A approximation of a wavy water surface. The top BBox face is shifted
along `y = ymax + amp · h(x, z)` where

$$h(x,z) = \sin(k_1x+\varphi_1)\cos(k_1z+\varphi_2) + \tfrac{1}{2}\sin(k_2(x+z)+\varphi_3)$$

Each ray that would hit the *top* face has its `t` offset by
`amp · h(x, z) / r.d.y` — a first-order solution of the displaced-plane
intersection. This makes ray-segment endpoints (x,z)-dependent without
touching geometry, which:

- modulates path length through the medium, and therefore Beer-Lambert
  transmittance, across the (x,z) grid (visible undulating waterline);
- modulates t_enter on shadow rays (visible god-ray spatial variation).

Defaults from [raytraced_renderer.cpp:75](../src/pathtracer/raytraced_renderer.cpp):
`amp = 0.035`, `freq = 8.0`.

**TA might ask** *"what's wrong with this approximation?"* — Two things:
(1) it does not change the surface's *normal*, so refraction direction
through the boundary is unchanged → no caustic focusing; (2) it ignores
multi-valued intersections that would happen at high amplitudes (steep waves
fold back on themselves).

### 3.9 Shading-normal water perturbation (`triangle.cpp:28`)

Plan B alternative. For upward-facing GlassBSDF triangles in a scene, replace
the shading normal at the hit with the analytic gradient of the same height
field:

$$\mathbf{n}_{\mathrm{shade}}=\mathrm{normalise}(-\partial h/\partial x,\,1,\,-\partial h/\partial z)$$

Geometry stays a flat quad, but the BSDF coordinate frame becomes
spatially varying — Snell at this triangle now bends rays in different
directions across the surface, the way a real wavy mesh would.

Off by default. Enable per-render via `WATER_NORMAL_AMP=0.04`.

**TA might ask** *"why both Plan A and Plan B?"* — A modulates
transmittance/path length (cheap, gives the visual undulating waterline).
B modulates per-ray refraction direction (gives the warped-view effect when
looking through the surface). They address different visual phenomena;
either or both can be enabled depending on what you want to demo.

### 3.10 Recursive `at_least_one_bounce_radiance` (`pathtracer.cpp:402`)

Standard surface-only path-tracing recursion:

```
if !is_delta:  L += direct lighting via NEE
if depth ≥ max_ray_depth: return
for num_samples (= ns_refr if delta else ns_diff):
    f, wi, pdf = bsdf->sample_f(wo)
    indirect += f · est_radiance_global_illumination(spawn(wi)) · cos / pdf
return L + indirect / num_samples
```

The recursion is via `est_radiance_global_illumination`, which means each
bounce ray correctly accumulates volumetric in-scatter and Beer-Lambert
attenuation along its segment (instead of plain BVH lookup).

**TA might ask** *"is this Russian roulette?"* — Strictly no, this is a
hard-cap depth termination. We chose a hard cap because the underwater
scenes have predictable bounce depth (camera ↔ water ↔ object), so RR's
de-biasing complexity isn't worth the variance reduction.

**TA might ask** *"why split ns_diff and ns_refr?"* — Delta BSDFs have
zero variance per sample (the lobe is a Dirac), so we want few samples
per bounce on glass / water (`ns_refr = 1` typical). Diffuse surfaces
have hemisphere-spread radiance, so we want more (`ns_diff = 1` is the
project default but can be raised for cleaner indirect).

### 3.11 Beer-Lambert `det_transmittance` (`medium.h:104`)

Per-channel transmittance along a homogeneous segment:

$$T(s) = \exp(-\boldsymbol{\sigma}_t s) = (e^{-\sigma_{t,R}s},\,e^{-\sigma_{t,G}s},\,e^{-\sigma_{t,B}s})$$

For heterogeneous medium (smoothstep height falloff in y), the optical
depth `τ` is integrated by 32-step midpoint quadrature; the resulting
exponential is still per-channel.

The chosen σ for the underwater scene
([raytraced_renderer.cpp:73](../src/pathtracer/raytraced_renderer.cpp)):

| Channel | σ_a | σ_s | σ_t |
|---------|-----|-----|-----|
| R | 0.35 | 0.20 | 0.55 |
| G | 0.10 | 0.25 | 0.35 |
| B | 0.03 | 0.30 | 0.33 |

Red is absorbed ~5× more than blue → underwater colour shifts toward
teal/blue with depth. Single-scattering albedo `σ_s / σ_t` is highest in
blue → blue scatters more visibly.

---

## 4 · Tunable parameters reference

All can be overridden at render time via environment variables — no rebuild.

| Variable | Default | Recommended range | Effect |
|----------|---------|-------------------|--------|
| `MEDIUM_SIGMA_A_R/G/B` | 0.35 / 0.10 / 0.03 | tens of percent up | absorption per channel |
| `MEDIUM_SIGMA_S_R/G/B` | 0.20 / 0.25 / 0.30 | tens of percent up | scattering per channel |
| `MEDIUM_HG_G` | 0.75 | -0.95 to +0.95 | phase asymmetry; >0 forward |
| `MEDIUM_BOUNDARY_AMP` | 0.035 | 0.0 (flat) – 0.12 | top-face wavy displacement |
| `MEDIUM_BOUNDARY_FREQ` | 8.0 | 4 – 20 | spatial frequency of waves |
| `MEDIUM_MAX_DIST` | +∞ | scene-extent | shadow-ray cap |
| `WATER_NORMAL_AMP` | 0.0 (off) | 0.02 – 0.08 | shading-normal perturbation magnitude on Glass triangles |
| `WATER_NORMAL_FREQ` | 8.0 | 4 – 20 | shading-normal perturbation frequency |

CLI flags (existing CS184):
- `-s` samples per pixel
- `-l` light samples
- `-m` max ray depth
- `-r WIDTH HEIGHT`
- `-t` thread count
- `-f` output PNG

---

## 5 · Limitations / known trade-offs (be honest with the TA)

1. **No real caustics.** `shadow_ray_blocked` skips refraction for direct-light
   shadow rays, so the floor of the underwater scene shows no caustic
   focusing pattern. The visible "wavy waterline" is from boundary
   perturbation, *not* from refractive focusing.

2. **No multiple scattering.** Volume integration is single-scatter only.
   For the chosen σ this is a fine approximation (single-scatter albedo ≤
   0.55 × the dominant channel), but in dense fog / cloud media multiple
   scattering would dominate.

3. **No (η_t / η_i)² radiance scaling** on transmitted rays. Small
   energy-conservation bias on refracted radiance.

4. **Hard depth cap, no Russian roulette.** Above `max_ray_depth` the path
   is dropped. For the chosen depth (5–6) and our scenes this contributes
   negligible variance, but a more aggressive scene with deep specular
   chains would lose energy.

5. **Wavy boundary is first-order.** The ray–displaced-plane intersection
   uses a Newton-iteration-of-one: evaluate `h` at the unperturbed (x, z)
   and shift `t`. Accurate to O(amp²); fine for our amplitudes (≤ 0.1), but
   a steep wave would need a real fixed-point iteration.

6. **`shadow_ray_blocked` is biased.** See §3.5. The trade is variance vs.
   bias; we picked bias.

---

## 6 · The hard questions a TA might ask

> **Q: Walk me through what happens when a camera ray hits a water
> surface and decides to refract.**

The primary ray reaches the BVH-intersected glass triangle. Control returns
to `est_radiance_global_illumination`, which calls `at_least_one_bounce_radiance`
on that intersection. Because the BSDF is delta, NEE direct lighting is
skipped. We loop `ns_refr` times (typically 1) calling
`GlassBSDF::sample_f(wo, &wi, &pdf)`. That function computes the dielectric
Fresnel coefficient `fr` from cos θ and the IOR ratio, calls
`refract` to compute the transmitted direction `wt`, then either reflects
(probability `fr`) or refracts (probability `1−fr`). The chosen `wi` is
returned in local frame; we transform to world, spawn a new ray with depth
incremented and origin offset by EPS_F to dodge self-intersection, and
recursively call `est_radiance_global_illumination` on it. That recursion
will accumulate volumetric in-scatter on its own segment, hit some
underwater surface, and integrate again. The contribution is multiplied by
`f · |cos θ| / pdf` and accumulated.

> **Q: Why does the bunny look lit at all? The shadow ray to the ceiling
> light has to cross the water glass surface — wouldn't that make the
> bunny pitch black?**

Without `shadow_ray_blocked` it would. We treat glass / refraction surfaces
as transparent for shadow-ray visibility tests — the shadow ray walks
through them up to 16 times. This is a deliberate biased shortcut. It
makes underwater direct lighting work in 64 spp instead of needing
thousands. The trade is no real caustics on the floor.

> **Q: What's the unbiased way to do this?**

Photon mapping (precompute photons from the light through the wavy water
surface, deposit them on the floor; then estimate caustic radiance by
density estimation), or bidirectional path tracing, or manifold next-event
estimation. All are out of scope for this project.

> **Q: How does the wavy waterline show up if you didn't model an actual
> wavy mesh?**

Two complementary mechanisms.

(a) `MEDIUM_BOUNDARY_AMP > 0`: in `clip_ray`, whichever end of the BVH
intersection corresponds to the medium's top face has its `t` displaced by
`amp · h(x, z) / r.d.y`. This means the *length* of the in-medium segment
varies across (x, z), which modulates Beer-Lambert transmittance across the
waterline — visible undulation along walls in the back of the scene.

(b) `WATER_NORMAL_AMP > 0`: in `Triangle::intersect`, upward-facing glass
triangles get their shading normal replaced by the analytic gradient of the
same height field. The geometry stays flat but `GlassBSDF` then computes
Snell against the perturbed normal, so the refracted direction varies
across the surface.

> **Q: What is `g` in Henyey-Greenstein? Why 0.75? What if I set it to 0?**

`g` is the asymmetry parameter, the mean cosine of the scattering angle.
`g = 0` is isotropic, `g → 1` is forward-peaked, `g → −1` is back-peaked.
Ocean water typically uses `g ≈ 0.85`; we picked 0.75 as a softer-edged
default. At `g = 0` the integrand becomes rotationally symmetric in
direction and god rays disappear — the volume looks like uniform fog.

> **Q: What's the unit of σ_a, σ_s, σ_t?**

Inverse length. Beer-Lambert says `T(s) = exp(-σ_t · s)` for path length
s — so σ has units of `1/length`. With our scene at world units roughly 2
across, `σ_t = 0.55` for red gives transmittance `exp(-1.1) ≈ 0.33` across
the box. That's why red attenuates noticeably; blue's `σ_t = 0.33` yields
`exp(-0.66) ≈ 0.52`.

> **Q: Why MIS instead of just sampling the light, or just the medium?**

Light-only sampling (equiangular) has variance ~∞ for points far from the
light (its pdf→0 there but the integrand is non-zero). Medium-only sampling
(truncexp) misses the sharp peak right at the light's closest projection
onto the ray. MIS uses each where it's good — pdf² weighting (β=2) damps
the contribution from a bad strategy at any given point. Variance is
dominated by `min(pdf_a², pdf_b²)` instead of `min(pdf_a, pdf_b)`.

> **Q: Why is `est_radiance_global_illumination` recursive?**

To carry volumetric in-scatter and Beer-Lambert transmittance through every
bounce ray. If we just did `bvh->intersect(bounce); ...` we'd lose the
bounce ray's own volumetric path. By calling the integrator recursively we
get the full transport, at the cost of duplicate volumetric integration on
the same segment if it's hit by multiple rays.

> **Q: What's the asymptotic complexity? Time per pixel?**

O(spp · max_depth · ns_diff · BVH_depth · medium_quadrature). For our
scenes: 64 spp · 5 depth · 1 sample · log(28000 prims) · O(1) closed-form
Beer-Lambert → ~5–20s per 480×480 frame on 8 threads.

> **Q: What stops the recursion at a glass surface from going forever?**

The hard `r.depth >= max_ray_depth` check at line 417. A glass-glass-glass
chain is bounded by max_depth (default 5–6).

> **Q: Are your BSDFs energy-conserving?**

Up to the (η_t/η_i)² scaling we omit, yes. `GlassBSDF` returns `reflectance · fr`
and `transmittance · (1−fr)` weighted; with the conservative material
inputs `reflectance = transmittance = 1`, the total reflected + refracted
radiance is exactly `1 · fr + 1 · (1−fr) = 1`. Adding the η² factor would
be a 5-line change; we left it off because the visual difference is
negligible at 1.33 IOR and it adds a debug variable.

---

## 7 · Live-demo recipes

### 7.1 Side-by-side: with vs without volume

```bash
# Compile-time disable: comment out medium creation in raytraced_renderer.cpp.
# Or render a non-medium scene like CBspheres.dae to show the path tracer
# alone.
./build/pathtracer -s 256 -l 4 -m 5 -r 480 480 \
    -f no_medium.png dae/sky/CBspheres_lambertian.dae

./build/pathtracer -s 256 -l 4 -m 5 -r 480 480 \
    -f with_medium.png dae/sky/CBbunny_water.dae
```

### 7.2 Knob: Henyey-Greenstein g

```bash
MEDIUM_HG_G=0.0  ./build/pathtracer -s 256 ... -f g0.png  ...
MEDIUM_HG_G=0.5  ./build/pathtracer -s 256 ... -f g50.png ...
MEDIUM_HG_G=0.85 ./build/pathtracer -s 256 ... -f g85.png ...
```

Talk track: "as I crank g up the visible god-ray brightness along the
forward-aligned camera-light axis grows."

### 7.3 Knob: chromatic absorption

```bash
MEDIUM_SIGMA_A_R=0.7 MEDIUM_SIGMA_A_G=0.05 MEDIUM_SIGMA_A_B=0.02 \
    ./build/pathtracer ... -f deep_blue.png ...
```

Talk track: "raising σ_a in red makes the water turn cyan / deeper sea
blue. This is just per-channel Beer-Lambert."

### 7.4 Knob: surface ripples

```bash
WATER_NORMAL_AMP=0.0  ./build/pathtracer ... -f flat.png ...
WATER_NORMAL_AMP=0.04 ./build/pathtracer ... -f wavy.png ...
WATER_NORMAL_AMP=0.08 ./build/pathtracer ... -f choppy.png ...
```

Talk track: "the water mesh is still a flat quad — only the shading normal
changes per ray. Real Snell refraction against that perturbed normal is
what bends the underwater view."

---

## 8 · References

- Kulla & Fajardo, *Importance Sampling Techniques for Path Tracing in
  Participating Media*, EGSR 2012. Equiangular sampling derivation.
- Henyey & Greenstein, *Diffuse radiation in the galaxy*, Astrophys. J. 1941.
  The HG phase function.
- PBRT v3 chapters 8 (Reflection Models), 11 (Volume Scattering),
  15.4 (Volume Direct Lighting).
- Veach & Guibas, *Multiple Importance Sampling*, SIGGRAPH 1995.
- Schlick, *An Inexpensive BRDF Model for Physically-based Rendering*,
  Eurographics 1994. We did NOT use this; included for reference because
  TAs commonly ask about it.

---

## Appendix A · Cheat for "what value would I set if…"

| You want… | Set |
|-----------|-----|
| more saturated cyan water | σ_a R↑, B↓ |
| visible god rays | σ_s↑ + g≥0.7 + small bright light + reasonable scattering albedo |
| visible wavy waterline along walls | `MEDIUM_BOUNDARY_AMP=0.05` `MEDIUM_BOUNDARY_FREQ=10` |
| warped view through water | `WATER_NORMAL_AMP=0.05` (Plan B; needs glass triangle in scene) |
| less noise underwater | `-s 256` and `-l 8`; `ns_diff=2` |
| fewer fireflies on glass-bunny | `ns_refr=1`, max_ray_depth ≥ 5 |
