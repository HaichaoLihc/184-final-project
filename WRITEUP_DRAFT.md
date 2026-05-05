# Underwater Volumetric Rendering Writeup Draft

## Abstract

This project extends a path tracer with underwater rendering effects. The system supports direct volume lighting, volumetric photon mapping, VPM acceleration with BRE/BVH lookup, surface caustic photon mapping, water-wave normals, wavy water boundaries, chromatic absorption, and anisotropic volume scattering. The goal is to render underwater scenes where light refracts through the water surface, scatters inside the medium, attenuates with depth, and forms caustic patterns on surfaces.

## Technical Approach

### Base Renderer

- CS184 path tracer
- Surface BSDFs
- Direct lighting
- Indirect lighting
- Reflection
- Refraction
- Scene BVH
- Shadow rays
- Russian roulette for indirect surface paths

### Participating Medium

- Homogeneous underwater medium
- Absorption coefficient
- Scattering coefficient
- Extinction coefficient
- Transmittance
- Beer-Lambert attenuation
- Chromatic absorption
- Henyey-Greenstein phase function
- Forward scattering parameter

### Direct Volume Rendering

- Ray marching through water
- Single scattering
- In-scattering from area light
- Shadow ray visibility
- Equiangular sampling
- Distance sampling
- Multiple importance sampling
- Medium boundary clipping

### Water Surface

- Refractive water surface
- Fresnel term
- Air-to-water refraction
- Wavy normal perturbation
- Analytic wavy medium boundary
- Water wave amplitude
- Water wave frequency

### Volumetric Photon Mapping

- Photon emission from area light
- Photon refraction through water surface
- Volume photon storage
- Medium scattering events
- Photon power / throughput
- Henyey-Greenstein photon scattering
- Russian roulette for photon paths
- Photon gather radius
- Photon strength
- Photon count
- VPM spatial grid

### VPM BRE / BVH Acceleration

- Bounding Region Estimation
- Photon influence bounds
- BVH over volume photons
- Ray segment query
- Beam-like photon lookup
- Faster VPM lookup
- Comparison against uniform grid lookup

### Surface Caustic Photon Mapping

- Caustic photon tracing
- Specular water entry path
- Surface photon storage
- Diffuse surface gather
- Surface caustic radius
- Surface caustic strength
- Separate surface caustic grid
- Russian roulette for caustic photon paths

### Rendering Modes

- Normal path tracing
- No volume lighting
- Direct volume only
- Direct volume with surface caustics
- Point VPM
- VPM with BRE/BVH
- VPM with surface caustics
- Combined underwater render

### Differences From References

- Implemented subset of full volumetric photon mapping
- Point-based VPM instead of full beam photon mapping
- BRE/BVH used as acceleration for volume photon lookup
- Separate surface caustic map for visible refracted patterns
- Analytic wavy water boundary instead of full dynamic simulation
- Environment-variable feature switches for comparison renders

## Problems Encountered

- Volume noise
- Photon map speckle
- Blue light artifacts / photon variance
- Balancing gather radius vs sharpness
- Direct volume and VPM looking visually similar in simple scenes
- Long render times for high-sample VPM BRE renders

## Lessons Learned

- Direct volume gives a clean baseline
- VPM needs suitable scenes to show visible differences
- Surface caustics are more visually obvious than pure volume photon effects
- Photon count, radius, and strength strongly affect image quality
- Acceleration structures matter for photon lookup performance
- Higher samples reduce noise but render time grows quickly
- Separate feature switches make debugging and comparison easier

## Results

High-quality comparison folder:

- `render3/high_quality_comparison/00_direct_volume_only_720x540_ultra_waves.png`
- `render3/high_quality_comparison/01_direct_volume_surface_caustics_1280x960_32spp.png`
- `render3/high_quality_comparison/02_vpm_bre_no_surface_caustics_1280x960_64spp.png`
- `render3/high_quality_comparison/03_vpm_bre_with_surface_caustics_1280x960_64spp.png`
- `render3/high_quality_comparison/04_vpm_bre_no_surface_caustics_640x480_256spp_low_noise.png`

Metadata:

- `render3/high_quality_comparison/metadata.csv`
- `render3/high_quality_comparison/README.md`

Performance comparison:

- VPM grid lookup
- VPM BRE/BVH lookup
- BRE speedup comparison renders

## References

- Jensen, Henrik Wann. Global Illumination using Photon Maps.
- Jensen, Henrik Wann and Christensen, Per. Efficient Simulation of Light Transport in Scenes with Participating Media using Photon Maps.
- Jarosz, Wojciech et al. A Comprehensive Theory of Volumetric Radiance Estimation using Photon Points and Beams.
- Pharr, Jakob, Humphreys. Physically Based Rendering: From Theory to Implementation.
- CS184 Path Tracer framework.

## Contributions

### Team Member 1

- Direct volume rendering
- Medium model
- MIS sampling
- Water attenuation
- Render comparisons

### Team Member 2

- Volumetric photon mapping
- VPM grid lookup
- BRE/BVH acceleration
- Surface caustic photon mapping
- High-quality final renders

### Shared

- Debugging
- Scene design
- Parameter tuning
- Writeup
- Presentation images
