# Underwater Rendering Comparison Images

This folder contains the rendered images used to compare the underwater volume
and caustic modes.

`*_rate.png` files are renderer sample-rate/debug outputs for the matching
image.

## 00_quick_smoke

Early low-sample VPM smoke renders used to verify that the renderer and water
wave setup were working.

## 01_volume_modes

Same underwater sphere scene with different volume-lighting modes:

- no volume lighting
- direct volume lighting
- point-based VPM
- VPM with BRE acceleration

## 02_blue_spot_cleanup

Renders used while diagnosing the blue VPM photon speckle/spot issue and
tuning radius/strength.

## 03_surface_caustics

Surface caustic photon map comparison:

- no surface caustic map
- surface caustic map smoke test
- higher-quality direct-volume render with surface caustics enabled

## 04_vpm_demo_scene

Renders of the generated demo scene designed to make VPM/BRE differences more
visible.

## 05_final_pebbles

Pebble scene render with direct volume, VPM, BRE, surface caustics, and water
waves enabled.
