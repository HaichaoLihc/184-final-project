# Current Feature Summary

| Feature | Status | Switch / Params | Notes |
|---|---:|---|---|
| Chromatic underwater medium | Done | `MEDIUM_SIGMA_A*`, `MEDIUM_SIGMA_S*` | RGB absorption/scattering for blue-green underwater attenuation |
| Henyey-Greenstein phase | Done | `MEDIUM_HG_G` | Default around `0.75`, forward-scattering water look |
| Direct volume single scattering | Done | `VOLUME_DIRECT_ENABLE=1` | Equiangular + truncated-exponential MIS direct lighting baseline |
| VPM point photon map | Done | `VPM_ENABLE=1` | Emits and stores volume scattering photon points |
| Mitsuba-style BRE over VPM | Done, recommended VPM mode | `VPM_BRE_ENABLE=1` | Camera beam x photon points; fast volumetric photon lookup |
| Adaptive BRE photon radius | Done | `VPM_BRE_LOOKUP_SIZE`, `VPM_BRE_MAX_RADIUS`, `VPM_BRE_RADIUS_SCALE` | Local-density photon radius estimate, inspired by Mitsuba BRE |
| BRE BVH | Done | `VPM_BRE_LEAF_SIZE` | Bounding hierarchy over photon spheres |
| Primary-ray BRE cap | Done, default on | `VPM_BRE_MAX_QUERY_DEPTH=0` | Avoids expensive secondary volume BRE queries |
| Legacy VPM gather | Fallback | `VPM_BRE_ENABLE=0` | Older ray-marched point gather path |
| Beam x Beam 1D blur | Experimental | `VBM_ENABLE=1` | Paper-style beam-beam estimator exists, but slow in this renderer |
| Beam BVH | Experimental | `VBM_ENABLE=1`, `VBM_BVH_LEAF_SIZE` | Helps beam-beam but long beam AABBs still overlap heavily |
| Wavy medium boundary | Done | `MEDIUM_BOUNDARY_AMP`, `MEDIUM_BOUNDARY_FREQ` | Wavy water boundary for photon entry and medium clipping |
| Visible water normal waves | Done | `WATER_NORMAL_AMP`, `WATER_NORMAL_FREQ` | Perturbs glass water surface shading normals |
| Pebble scatter scene | Done | `dae/sky/CBspheres_underwater_pebbles.dae` | 95 procedural floor pebbles, radius about `[0.01, 0.05]` |
| Render stats | Done | automatic | Prints BVH rays, intersection tests, BRE query/node/photon stats |
| VPM+BRE render | Done | `render3/CBspheres_underwater_pebbles_bre_hq_waves.png` | High-quality photon-volume comparison render |
| Direct volume render | Done | `render3/CBspheres_underwater_pebbles_direct_volume_hq_waves.png` | Same scene/config direct-volume comparison render |
| Higher-config direct render | Done | `render3/CBspheres_underwater_pebbles_direct_volume_ultra_waves.png` | `720x540`, `160 spp` direct-volume render |

