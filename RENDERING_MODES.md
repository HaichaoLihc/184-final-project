# Rendering Modes and Features

Quick reference for the underwater renderer modes and the main switches used to
turn them on/off.

## Main Rendering Modes

| Mode | Main switches | What it renders | Intuition | Best used for |
|---|---|---|---|---|
| Normal path tracing | default | Surface lighting, reflections, refractions, shadows | Regular CS184 path tracer behavior | Baseline image without special underwater effects |
| No volume lighting | `VOLUME_DIRECT_ENABLE=0 VPM_ENABLE=0` | Surfaces seen through water attenuation, but no glowing/scattering water | Water darkens/colors rays, but the water itself does not glow | Debugging surface appearance and water attenuation |
| Direct volume | `VOLUME_DIRECT_ENABLE=1 VPM_ENABLE=0` | Single-scattered light in the water, sampled directly from lights | Ray marching through water and asking “can this point see the light?” | Clean underwater haze from direct light |
| Point VPM | `VPM_ENABLE=1 VPM_BRE_ENABLE=0` | Volume photon map using stored scattering points | Photons are stored where light scatters in the water; camera rays gather nearby photons | Showing indirect/forward-scattered volume light |
| VPM + BRE | `VPM_ENABLE=1 VPM_BRE_ENABLE=1` | Same point VPM result, but queried with bounding-radius estimation | Each photon has an influence box; a BVH skips photons far from the camera ray | Faster/lower-noise VPM ray queries |
| Surface caustics | `CAUSTIC_ENABLE=1` | Focused light patterns on diffuse surfaces | Photons refract through water/specular objects, then get stored where they hit surfaces | Floor/sphere caustic highlights |
| Combined underwater render | `VOLUME_DIRECT_ENABLE=1 VPM_ENABLE=1 VPM_BRE_ENABLE=1 CAUSTIC_ENABLE=1` | Direct volume, VPM, BRE acceleration, surface caustics, and surfaces together | Full current feature set | Final presentation renders |

## Supporting Features

| Feature | Main switches | What it does | Intuition |
|---|---|---|---|
| Water wave normals | `WATER_NORMAL_AMP`, `WATER_NORMAL_FREQ` | Perturbs water-surface normals | Makes the water surface visually wavy and bends reflected/refracted light |
| Wavy medium boundary | `MEDIUM_BOUNDARY_AMP`, `MEDIUM_BOUNDARY_FREQ` | Makes the analytic water-air boundary wavy for photon refraction | Photons enter water through a moving-looking wave surface |
| Chromatic absorption | `MEDIUM_SIGMA_A_R/G/B` or `MEDIUM_SIGMA_A` | Absorbs RGB channels differently | Red disappears faster underwater; blue/green survive farther |
| Volume scattering | `MEDIUM_SIGMA_S_R/G/B` or `MEDIUM_SIGMA_S` | Controls how much light scatters in water | Higher scattering means brighter/milkier water |
| Forward scattering | `MEDIUM_HG_G` | Controls Henyey-Greenstein phase anisotropy | Higher values push light forward, like underwater haze shafts |
| Photon count | `VPM_PHOTONS`, `CAUSTIC_PHOTONS` | Controls how many photons are traced | More photons usually means smoother photon-map results, but slower setup |
| Gather radius | `VPM_RADIUS`, `CAUSTIC_RADIUS` | Controls photon smoothing radius | Larger radius is smoother/blotchier; smaller radius is sharper/noisier |
| Photon strength | `VPM_STRENGTH`, `CAUSTIC_STRENGTH` | Scales photon contribution | Useful for matching visual intensity during demos |
| Grid resolution | `VPM_GRID_RES`, `CAUSTIC_GRID_RES` | Controls spatial grid resolution | Higher grids reduce local lookup work but use more memory |
| Max photon depth | `VPM_MAX_DEPTH`, `CAUSTIC_MAX_DEPTH` | Controls photon bounces | More depth captures more multi-bounce/specular paths |

## Acceleration Structures

| Structure | Used by | Query shape | Why it exists |
|---|---|---|---|
| Scene BVH | All camera, shadow, reflection, refraction, and photon rays | Ray vs. geometry | Avoid testing every ray against every triangle/sphere |
| VPM grid | Plain point VPM | Point lookup in water | Quickly find photons near a sampled point |
| VPM BRE BVH | VPM + BRE | Camera ray segment vs. photon influence boxes | Quickly find photons close to the whole ray segment |
| Surface caustic grid | Surface caustics | Surface hit point lookup | Quickly find caustic photons near a diffuse hit point |

## Common Example Configs

| Goal | Example switches |
|---|---|
| Clean direct-volume comparison | `VOLUME_DIRECT_ENABLE=1 VPM_ENABLE=0 CAUSTIC_ENABLE=0` |
| Plain VPM comparison | `VOLUME_DIRECT_ENABLE=0 VPM_ENABLE=1 VPM_BRE_ENABLE=0 CAUSTIC_ENABLE=0` |
| Faster VPM comparison | `VOLUME_DIRECT_ENABLE=0 VPM_ENABLE=1 VPM_BRE_ENABLE=1 CAUSTIC_ENABLE=0` |
| Surface caustics only on surfaces | `VOLUME_DIRECT_ENABLE=0 VPM_ENABLE=0 CAUSTIC_ENABLE=1` |
| Full underwater render | `VOLUME_DIRECT_ENABLE=1 VPM_ENABLE=1 VPM_BRE_ENABLE=1 CAUSTIC_ENABLE=1` |
