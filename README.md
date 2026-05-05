# Project 3-1: Pathtracer




This renderer has four main underwater lighting features.

  First is direct volume rendering. When a camera ray goes through water, we sample points along that ray. At each
  point, we estimate in-scattering, which means light from the lamp scatters inside the water toward the camera. To
  choose good sample points, we use MIS, or multiple importance sampling, combining equiangular sampling and distance
  sampling. This gives us a clean baseline.

  Second is volumetric photon mapping. First, we trace photons from the light. In the underwater scene, photons refract through the water surface, travel
  in the medium, and store volume photons when they scatter.
  Then, during camera rendering, we sample points along the camera ray inside the water. At each point, we query
  nearby photons in the photon map. Those photons estimate the incoming light at that point, and the phase function
  converts it into in-scattered light toward the camera.

  Third is VPM with BRE / BVH acceleration. The basic VPM version samples many points along the camera ray and
  searches for photons around each point. Our faster version builds a BVH over the photon map and does a ray-based
  gather. This is closer to beam radiance estimation, or BRE. The camera ray acts like a beam, and the renderer finds
  photon regions that overlap that beam. 

  Fourth is surface caustics. Some photons pass through the water, refract, and hit diffuse surfaces like the floor
  or spheres. We store those photons in a separate surface caustic map. When a camera ray hits a surface, we gather
  nearby caustic photons to create the bright focused light patterns underwater.

  So the pipeline is: direct volume gives a physical baseline, VPM adds photon-based underwater scattering, BRE/BVH
  makes the photon lookup faster, and surface caustics add the refracted light patterns on solid objects.
