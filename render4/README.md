# Render4 Showcase Set

Standard settings for this folder:

- Scene: `dae/sky/CBspheres_underwater_pebbles.dae`
- Resolution: `720x540`
- Samples: `128 spp`
- Light samples: `12`
- Max ray depth: `8`

## Images

| File | Purpose |
|---|---|
| `00_no_volume_baseline_720x540_128spp.png` | Baseline with volume lighting off. |
| `01_direct_volume_720x540_128spp.png` | Direct volume only. |
| `02_blue_artifact_before_vpm_720x540_128spp.png` | Challenge case: blue photon variance artifacts from an unstable VPM setting. |
| `03_blue_artifact_after_vpm_720x540_128spp.png` | Improved VPM setting that reduces the artifact with more photons, smoother radius, and lower strength. |
| `04_vpm_grid_clean_720x540_128spp.png` | VPM using grid lookup. |
| `05_vpm_bre_clean_720x540_128spp.png` | VPM using BRE/BVH lookup. |
| `06_caustics_off_720x540_128spp.png` | Full volume render with surface caustics off. This is copied from `05` because it is the same configuration. |
| `07_caustics_on_720x540_128spp.png` | Same setup with surface caustics on. |
| `09_sigma_t_2x_ripples_direct_volume_720x540_128spp.png` | Direct volume variation with doubled `sigma_t` and visible water ripples. |
| `11_sigma_t_4x_ripples_direct_volume_720x540_128spp.png` | Direct volume variation with `4x sigma_t` to show stronger volumetric extinction and scattering. |

Metadata is tracked in `metadata.csv`.

## Challenge: Blue Artifacts

The blue artifact pair documents a problem we had to overcome in VPM: sparse or overly strong volume photons can create bright blue blotches in the water. The improved render reduces the issue by increasing photon count, using a larger gather radius, and lowering photon-map contribution strength.

## Webpage Comparisons

| Comparison | Images |
|---|---|
| Volume baseline | `00_no_volume_baseline_720x540_128spp.png`, `01_direct_volume_720x540_128spp.png` |
| Blue artifact before/after | `02_blue_artifact_before_vpm_720x540_128spp.png`, `03_blue_artifact_after_vpm_720x540_128spp.png` |
| Same-camera direct volume vs VPM | `01_direct_volume_720x540_128spp.png`, `03_blue_artifact_after_vpm_720x540_128spp.png` |
| VPM grid vs VPM BRE | `04_vpm_grid_clean_720x540_128spp.png`, `05_vpm_bre_clean_720x540_128spp.png` |
| Surface caustics off/on | `06_caustics_off_720x540_128spp.png`, `07_caustics_on_720x540_128spp.png` |
| Direct volume scattering variations | `01_direct_volume_720x540_128spp.png`, `09_sigma_t_2x_ripples_direct_volume_720x540_128spp.png`, `11_sigma_t_4x_ripples_direct_volume_720x540_128spp.png` |

## Suggested Webpage Flow

| Webpage section | Images | What the images show |
|---|---|---|
| Motivation / baseline | `00_no_volume_baseline_720x540_128spp.png`, `01_direct_volume_720x540_128spp.png` | Why underwater participating media matters: without volume lighting the scene is mostly surface shading, while direct volume adds underwater haze and in-scattering. |
| Direct volume method | `01_direct_volume_720x540_128spp.png` | Clean baseline for ray-marched single scattering with MIS sampling in the water volume. |
| Challenge: VPM photon variance | `02_blue_artifact_before_vpm_720x540_128spp.png`, `03_blue_artifact_after_vpm_720x540_128spp.png` | The main artifact we had to tune around: sparse/high-energy VPM photons produce blue blotches; more photons, smoother radius, and lower strength reduce the artifact. |
| Direct volume vs VPM | `01_direct_volume_720x540_128spp.png`, `03_blue_artifact_after_vpm_720x540_128spp.png` | Same-camera comparison between local direct volume estimation and photon-map-assisted volume lighting. |
| Acceleration structure | `04_vpm_grid_clean_720x540_128spp.png`, `05_vpm_bre_clean_720x540_128spp.png` | Visual comparison for grid lookup versus BRE/BVH lookup. This pair supports the performance/implementation discussion more than a dramatic visual difference. |
| Surface caustics | `06_caustics_off_720x540_128spp.png`, `07_caustics_on_720x540_128spp.png` | The effect of the surface caustic photon map on solid surfaces under water. |
| Direct volume parameter variation | `01_direct_volume_720x540_128spp.png`, `09_sigma_t_2x_ripples_direct_volume_720x540_128spp.png`, `11_sigma_t_4x_ripples_direct_volume_720x540_128spp.png` | These are not final-look renders. They show how changing the medium coefficients changes direct-volume scattering and extinction: `01` is the baseline, `09` increases `sigma_t` to 2x, and `11` increases `sigma_t` to 4x. |
