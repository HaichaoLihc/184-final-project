#ifndef CGL_PATHTRACER_H
#define CGL_PATHTRACER_H

#include "CGL/timer.h"

#include "scene/bvh.h"
#include "pathtracer/sampler.h"
#include "pathtracer/intersection.h"

#include "application/renderer.h"

#include "scene/scene.h"
using CGL::SceneObjects::Scene;

#include "pathtracer/medium.h"

#include "scene/environment_light.h"
using CGL::SceneObjects::EnvironmentLight;

using CGL::SceneObjects::BVHNode;
using CGL::SceneObjects::BVHAccel;

#include <atomic>
#include <vector>

namespace CGL {

    class PathTracer {
    public:
        PathTracer();
        ~PathTracer();

        /**
         * Sets the pathtracer's frame size. If in a running state (VISUALIZE,
         * RENDERING, or DONE), transitions to READY b/c a changing window size
         * would invalidate the output. If in INIT and configuration is done,
         * transitions to READY.
         * \param width width of the frame
         * \param height height of the frame
         */
        void set_frame_size(size_t width, size_t height);

        void write_to_framebuffer(ImageBuffer& framebuffer, size_t x0, size_t y0, size_t x1, size_t y1);

        /**
         * If the pathtracer is in READY, delete all internal data, transition to INIT.
         */
        void clear();

        void autofocus(Vector2D loc);

        /**
         * Trace an ray in the scene.
         */
        Vector3D estimate_direct_lighting_hemisphere(const Ray& r, const SceneObjects::Intersection& isect);
        Vector3D estimate_direct_lighting_importance(const Ray& r, const SceneObjects::Intersection& isect);
        Vector3D estimate_vol_direct_lighting(const Vector3D& p);

        // Equiangular + distance-sampling MIS integrator for single-scatter
        // direct lighting along a camera ray segment [t_enter, t_exit] that
        // lies inside the participating medium.
        Vector3D estimate_vol_direct_lighting_mis(const Ray& r,
                                                  double t_enter,
                                                  double t_exit);
        Vector3D estimate_vol_photon_lighting(const Ray& r,
                                              double t_enter,
                                              double t_exit);
        Vector3D estimate_surface_caustic_lighting(const Ray& r,
                                                   const SceneObjects::Intersection& isect);
        void build_volume_photon_map();
        void build_surface_caustic_map();
        void print_volume_photon_stats() const;
        void print_surface_caustic_stats() const;

        bool shadow_ray_blocked(const Ray& r) const;
        Vector3D est_radiance_global_illumination(const Ray& r);
        Vector3D zero_bounce_radiance(const Ray& r, const SceneObjects::Intersection& isect);
        Vector3D one_bounce_radiance(const Ray& r, const SceneObjects::Intersection& isect);
        Vector3D at_least_one_bounce_radiance(const Ray& r, const SceneObjects::Intersection& isect);
        
        Vector3D debug_shading(const Vector3D d) {
            return Vector3D(abs(d.r), abs(d.g), .0).unit();
        }

        Vector3D normal_shading(const Vector3D n) {
            return n * .5 + Vector3D(.5);
        }

        /**
         * Trace a camera ray given by the pixel coordinate.
         */
        void raytrace_pixel(size_t x, size_t y);

        // Integrator sampling settings //

        size_t max_ray_depth; ///< maximum allowed ray depth (applies to all rays)
        size_t isAccumBounces; ///< number of bounces to accumulate
        size_t ns_aa;         ///< number of camera rays in one pixel (along one axis)
        size_t ns_area_light; ///< number samples per area light source
        size_t ns_diff;       ///< number of samples - diffuse surfaces
        size_t ns_glsy;       ///< number of samples - glossy surfaces
        size_t ns_refr;       ///< number of samples - refractive surfaces

        size_t samplesPerBatch;
        double maxTolerance;
        bool direct_hemisphere_sample; ///< true if sampling uniformly from hemisphere for direct lighting. Otherwise, light sample

        // Components //

        BVHAccel* bvh;                 ///< BVH accelerator aggregate
        EnvironmentLight* envLight;    ///< environment map
        Sampler2D* gridSampler;        ///< samples unit grid
        Sampler3D* hemisphereSampler;  ///< samples unit hemisphere
        HDRImageBuffer sampleBuffer;   ///< sample buffer
        Timer timer;                   ///< performance test timer

        std::vector<int> sampleCountBuffer;   ///< sample count buffer

        Scene* scene;         ///< current scene
        Camera* camera;       ///< current camera

        Medium* medium = nullptr; ///< participating medium (null = vacuum)

        struct VolumetricPhotonMap {
            struct Photon {
                Vector3D position;
                Vector3D wi;     ///< direction from photon position toward light
                Vector3D power;  ///< photon flux carried to the scatter event
                double radius = 0.0;
                BBox bbox;
            };

            struct Node {
                BBox bbox;
                int start = 0;
                int count = 0;
                int left = -1;
                int right = -1;

                bool is_leaf() const { return left < 0 && right < 0; }
            };

            bool enabled = false;
            int nx = 0, ny = 0, nz = 0;
            BBox bounds;
            double radius = 0.08;
            double strength = 1.0;
            std::vector<Photon> photons;
            std::vector<std::vector<int>> cells;
            std::vector<int> bre_indices;
            std::vector<Node> bre_nodes;
            mutable std::atomic<unsigned long long> bre_query_count{0};
            mutable std::atomic<unsigned long long> bre_node_tests{0};
            mutable std::atomic<unsigned long long> bre_photon_tests{0};

            void clear() {
                enabled = false;
                nx = ny = nz = 0;
                photons.clear();
                cells.clear();
                bre_indices.clear();
                bre_nodes.clear();
                bre_query_count.store(0);
                bre_node_tests.store(0);
                bre_photon_tests.store(0);
            }

            bool valid() const {
                return enabled && nx > 1 && ny > 1 && nz > 1 &&
                       !photons.empty() &&
                       cells.size() == static_cast<size_t>(nx * ny * nz);
            }

            bool bre_valid() const {
                return valid() && !bre_nodes.empty() &&
                       bre_indices.size() == photons.size();
            }

            int index(int ix, int iy, int iz) const {
                return (iy * nz + iz) * nx + ix;
            }
        } volume_photon_map;

        struct SurfaceCausticPhotonMap {
            struct Photon {
                Vector3D position;
                Vector3D normal;
                Vector3D wi;     ///< direction from surface point toward light
                Vector3D power;  ///< photon flux arriving at the surface
            };

            bool enabled = false;
            int nx = 0, ny = 0, nz = 0;
            BBox bounds;
            double radius = 0.04;
            double strength = 1.0;
            std::vector<Photon> photons;
            std::vector<std::vector<int>> cells;

            void clear() {
                enabled = false;
                nx = ny = nz = 0;
                photons.clear();
                cells.clear();
            }

            bool valid() const {
                return enabled && nx > 1 && ny > 1 && nz > 1 &&
                       !photons.empty() &&
                       cells.size() == static_cast<size_t>(nx * ny * nz);
            }

            int index(int ix, int iy, int iz) const {
                return (iy * nz + iz) * nx + ix;
            }
        } surface_caustic_map;

        // Tonemapping Controls //

        double tm_gamma;                           ///< gamma
        double tm_level;                           ///< exposure level
        double tm_key;                             ///< key value
        double tm_wht;                             ///< white point
    };

}  // namespace CGL

#endif  // CGL_RAYTRACER_H
