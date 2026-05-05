# High Quality Underwater Render Comparison

This folder groups the clearest high-quality underwater renders into one comparison set.
The source scene for all entries is `dae/sky/CBspheres_underwater_pebbles.dae`.

## Images

| # | Image | Mode | Resolution | Samples | Surface caustics | VPM BRE | Notes |
|---|---|---|---:|---:|---|---|---|
| 00 | [00_direct_volume_only_720x540_ultra_waves.png](00_direct_volume_only_720x540_ultra_waves.png) | Direct volume only | 720x540 | unknown | Off | Off | Pure direct volume waves render, included as the simple baseline. |
| 01 | [01_direct_volume_surface_caustics_1280x960_32spp.png](01_direct_volume_surface_caustics_1280x960_32spp.png) | Direct volume + surface caustics | 1280x960 | 32 spp | On | Off | Direct volume lighting with surface caustic photon map enabled. |
| 02 | [02_vpm_bre_no_surface_caustics_1280x960_64spp.png](02_vpm_bre_no_surface_caustics_1280x960_64spp.png) | VPM + BRE | 1280x960 | 64 spp | Off | On | High-quality VPM volume lighting without surface caustic contribution. |
| 03 | [03_vpm_bre_with_surface_caustics_1280x960_64spp.png](03_vpm_bre_with_surface_caustics_1280x960_64spp.png) | VPM + BRE + surface caustics | 1280x960 | 64 spp | On | On | Same VPM settings as 02, plus surface caustic photon map. |
| 04 | [04_vpm_bre_no_surface_caustics_640x480_256spp_low_noise.png](04_vpm_bre_no_surface_caustics_640x480_256spp_low_noise.png) | VPM + BRE, low-noise | 640x480 | 256 spp | Off | On | Lower resolution, higher samples, larger VPM radius for reduced noise. |

The matching sample-rate images are in [rate_maps](rate_maps/).

## Metadata

| Image | Direct volume | VPM | VPM photons requested | VPM photons stored | VPM radius | VPM strength | Surface caustic photons requested | Surface caustic photons stored | Render time |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| 00 | On | Off | 0 | 0 | n/a | n/a | 0 | 0 | unknown |
| 01 | On | Off | 0 | 0 | n/a | n/a | 720000 | 413186 | 65.35s |
| 02 | On | On | 720000 | 406934 | 0.055 | 0.45 | 0 | 0 | 156.60s |
| 03 | On | On | 720000 | 406934 | 0.055 | 0.45 | 720000 | 413196 | 181.62s |
| 04 | On | On | 1200000 | 677480 | 0.065 | 0.42 | 0 | 0 | 1273.44s |

## Quick Read

- `00` is the pure direct-volume baseline.
- `01` shows direct volume lighting with the separate surface caustic photon map.
- `02` isolates VPM volume lighting with BRE acceleration and no surface caustics.
- `03` shows what the surface caustic map adds on top of the same VPM setup.
- `04` is the cleanest no-surface-caustics VPM result, trading render time for lower noise.
