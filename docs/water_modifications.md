# Underwater Water Surface and God-Ray Modifications

This document summarizes the implementation changes for the underwater water
surface, reflection/refraction support, and god-ray rendering path.

## Scene Changes

- `scripts/build_underwater_view.py` now generates a water surface for
  `dae/sky/CBspheres_underwater.dae`.
- The water surface is a two-triangle quad at world-space `y = 1.1`, matching
  the top of the bounded water medium.
- The generated scene includes a CGL `<glass>` material with:
  - `ior = 1.33`
  - white reflectance
  - white transmittance
  - zero roughness
- The existing refractive sphere material is also upgraded to a parsed CGL
  glass material instead of only a COLLADA common-profile Phong material.

## Specular BSDF Support

Implemented in `src/pathtracer/advanced_bsdf.cpp`.

- `BSDF::reflect` reflects an outgoing local-space direction about the local
  surface normal `(0, 0, 1)`.
- `BSDF::refract` applies Snell's law for air-to-dielectric and
  dielectric-to-air transitions, returning `false` under total internal
  reflection.
- `MirrorBSDF::sample_f` now samples the perfect reflection direction.
- `RefractionBSDF::sample_f` now samples the perfect transmission direction.
- `GlassBSDF::sample_f` now computes dielectric Fresnel reflectance and
  probabilistically samples either reflection or refraction.

These BSDFs are delta distributions, so their `f()` methods remain zero for
ordinary direct-light evaluation; their contribution comes from sampled
recursive rays.

## Recursive Radiance Transport

Implemented in `src/pathtracer/pathtracer.cpp`.

Before this change, `at_least_one_bounce_radiance()` returned zero, so reflected
or refracted rays did not fetch scene radiance. A glass or water hit could
choose a new direction, but no recursive light transport happened afterward.

The new path is:

1. A camera ray hits a surface.
2. Diffuse surfaces add direct lighting with `one_bounce_radiance()`.
3. The BSDF samples an incoming direction.
4. A new world-space ray is spawned from the hit point.
5. `est_radiance_global_illumination()` recursively evaluates that ray.
6. The returned radiance is weighted by the BSDF value, cosine factor, and PDF.

For delta materials such as glass and mirror, direct lighting is skipped and the
material is evaluated through its recursive reflected or refracted ray.

## Transparent Shadow Handling

Implemented in `PathTracer::shadow_ray_blocked()`.

Direct lighting and volumetric single scattering use shadow rays to ask whether
a point can see a light. Without special handling, the water surface is just a
BVH primitive, so a shadow ray from the underwater medium to the ceiling light
would hit the water surface and incorrectly treat it as opaque.

The new shadow test:

1. Intersects the shadow ray against the BVH.
2. If the hit surface is opaque, the light is blocked.
3. If the hit surface is `GlassBSDF` or `RefractionBSDF`, the ray skips just
   past that surface and continues.
4. The loop stops after a small fixed number of skipped transmissive surfaces.

This is a practical transparent-shadow approximation. It keeps glass/water from
blackening direct and volumetric lighting. It does not fully refract shadow rays
through Snell's law or generate physically exact caustics.

## Participating Medium and God Rays

Configured in `src/pathtracer/raytraced_renderer.cpp`.

The underwater scene uses a bounded participating medium below the water
surface:

- medium bounds: approximately the Cornell box interior below `y = 1.1`
- red absorption is strongest, blue absorption is weakest
- scattering is moderate and slightly blue-biased

To make god rays visible by default, the renderer now sets defaults when no
environment override is provided:

- `boundary_amp = 0.035`
- `boundary_freq = 8.0`
- `hg_g = 0.75`

The wavy boundary changes the effective water entry/exit distances over the
surface, creating spatial variation in light transmission. The positive
Henyey-Greenstein `g` value makes scattering forward-peaked, which strengthens
visible shafts when light, medium samples, and the camera are roughly aligned.

## Ray Lifecycle Summary

For a typical underwater pixel:

1. The camera emits a primary ray.
2. The ray is clipped against the water medium.
3. The renderer integrates volumetric single scattering along the segment.
4. For each volume sample, a shadow ray checks visibility to the light while
   skipping transmissive surfaces.
5. If the camera ray hits a surface, surface radiance is evaluated.
6. If that surface is water or glass, the BSDF samples reflection/refraction and
   recursively traces the next ray.
7. Surface radiance is attenuated by chromatic water transmittance.
8. Many samples are averaged into the final pixel.

## Final Render

The high-quality render was written to:

`renders/final_underwater_high_quality.png`

Render settings used:

- resolution: `768 x 576`
- camera samples: `64`
- area-light samples: `8`
- max ray depth: `6`
- threads: `8`
- water normal perturbation: `WATER_NORMAL_AMP=0.04`

