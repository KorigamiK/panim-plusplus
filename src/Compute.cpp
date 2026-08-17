#include "panim/Compute.hpp"
#include "panim/Log.hpp"

#include "ComputeBackend.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace panim {

    namespace {

        struct ComputeRuntime {
            ComputeDeviceInfo info;
            bool initialized = false;
            bool failure_logged = false;
        };

        ComputeRuntime &runtime() {
            static ComputeRuntime instance;
            return instance;
        }

        uint8_t to_byte(float value) {
            return static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
        }

        struct Float3 {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Float3 operator+(Float3 a, Float3 b) {
            return {a.x + b.x, a.y + b.y, a.z + b.z};
        }

        Float3 operator-(Float3 a, Float3 b) {
            return {a.x - b.x, a.y - b.y, a.z - b.z};
        }

        Float3 operator-(Float3 value) {
            return {-value.x, -value.y, -value.z};
        }

        Float3 operator*(Float3 value, float scale) {
            return {value.x * scale, value.y * scale, value.z * scale};
        }

        float dot(Float3 a, Float3 b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        Float3 cross(Float3 a, Float3 b) {
            return {a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x};
        }

        float length(Float3 value) {
            return std::sqrt(dot(value, value));
        }

        Float3 normalize(Float3 value) {
            float magnitude = std::max(length(value), 0.00001f);
            return value * (1.0f / magnitude);
        }

        Float3 mix(Float3 a, Float3 b, float amount) {
            return a + (b - a) * amount;
        }

        float mandelbulb_distance(Float3 point) {
            Float3 z = point;
            float derivative = 1.0f;
            float radius = 0.0f;
            for (int iteration = 0; iteration < 7; ++iteration) {
                radius = length(z);
                if (radius > 2.35f)
                    break;

                float safe_radius = std::max(radius, 0.00001f);
                float theta = std::acos(std::clamp(
                    z.z / safe_radius, -1.0f, 1.0f));
                float phi = std::atan2(z.y, z.x);
                float radial_power = std::pow(safe_radius, 7.0f);
                derivative = 8.0f * radial_power * derivative + 1.0f;

                float powered_radius = radial_power * safe_radius;
                float powered_theta = theta * 8.0f;
                float powered_phi = phi * 8.0f;
                z = Float3{
                        std::sin(powered_theta) * std::cos(powered_phi),
                        std::sin(powered_theta) * std::sin(powered_phi),
                        std::cos(powered_theta)} *
                        powered_radius +
                    point;
            }
            float safe_radius = std::max(radius, 0.00001f);
            return 0.5f * std::log(safe_radius) * safe_radius /
                   std::max(derivative, 0.00001f);
        }

        Float3 mandelbulb_normal(Float3 point) {
            constexpr float epsilon = 0.0025f;
            Float3 x{epsilon, 0.0f, 0.0f};
            Float3 y{0.0f, epsilon, 0.0f};
            Float3 z{0.0f, 0.0f, epsilon};
            return normalize({
                mandelbulb_distance(point + x) -
                    mandelbulb_distance(point - x),
                mandelbulb_distance(point + y) -
                    mandelbulb_distance(point - y),
                mandelbulb_distance(point + z) -
                    mandelbulb_distance(point - z),
            });
        }

        Float3 render_mandelbulb(int x,
                                 int y,
                                 int width,
                                 int height,
                                 float time_seconds) {
            float screen_x = (x + 0.5f) / std::max(width, 1);
            float screen_y = (y + 0.5f) / std::max(height, 1);
            float aspect = static_cast<float>(width) /
                           std::max(height, 1);
            float uv_x = (screen_x * 2.0f - 1.0f) * aspect;
            float uv_y = 1.0f - screen_y * 2.0f;

            float orbit = time_seconds * 0.32f + 0.5f;
            Float3 camera{2.85f * std::cos(orbit),
                          0.65f + 0.18f * std::sin(orbit * 0.7f),
                          2.85f * std::sin(orbit)};
            Float3 forward = normalize(Float3{0.0f, 0.02f, 0.0f} - camera);
            Float3 right = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
            Float3 up = cross(right, forward);
            Float3 ray_direction = normalize(
                forward * 1.75f + right * uv_x + up * uv_y);

            float travel = 0.0f;
            bool hit = false;
            int step_index = 0;
            for (int step = 0; step < 52; ++step) {
                step_index = step;
                Float3 point = camera + ray_direction * travel;
                float distance = mandelbulb_distance(point);
                if (distance < 0.0025f) {
                    hit = true;
                    break;
                }
                travel += std::max(distance * 0.78f, 0.001f);
                if (travel > 6.5f)
                    break;
            }

            float horizon = std::clamp(
                0.5f + 0.5f * ray_direction.y, 0.0f, 1.0f);
            Float3 color = mix({7.0f, 10.0f, 24.0f},
                               {32.0f, 42.0f, 78.0f},
                               horizon);
            float view_glow = std::pow(
                std::max(dot(ray_direction, normalize(-camera)), 0.0f),
                12.0f);
            color = color + Float3{32.0f, 20.0f, 56.0f} * view_glow;

            if (hit) {
                Float3 point = camera + ray_direction * travel;
                Float3 normal = mandelbulb_normal(point);
                Float3 light = normalize({-0.45f, 0.8f, 0.35f});
                float diffuse = std::max(dot(normal, light), 0.0f);
                float facing = std::max(dot(normal, -ray_direction), 0.0f);
                float rim = std::pow(1.0f - facing, 2.4f);
                float detail = 0.5f + 0.5f * std::cos(
                                                 3.3f * point.y + 1.7f * point.x -
                                                 time_seconds * 0.18f);
                Float3 base = mix({40.0f, 105.0f, 210.0f},
                                  {235.0f, 92.0f, 182.0f},
                                  detail);
                float occlusion = 1.0f - 0.42f * step_index / 52.0f;
                color = base * ((0.13f + diffuse * 1.05f) * occlusion);
                color = color +
                        Float3{90.0f, 180.0f, 255.0f} * (rim * 0.72f);
                color = color + Float3{255.0f, 220.0f, 178.0f} *
                                    (std::pow(diffuse, 14.0f) * 0.5f);
            }
            return color;
        }

        void apply_cpu(Frame &frame,
                       ComputeEffect effect,
                       const ComputeParams &params) {
            float strength = std::clamp(params.strength, 0.0f, 1.0f);
            constexpr float tau = 6.28318530718f;
            float width_scale = frame.width > 1 ? 1.0f / (frame.width - 1) : 0.0f;
            float height_scale = frame.height > 1 ? 1.0f / (frame.height - 1) : 0.0f;

            for (int y = 0; y < frame.height; ++y) {
                float ny = y * height_scale;
                for (int x = 0; x < frame.width; ++x) {
                    float nx = x * width_scale;
                    uint8_t *pixel = frame.pixel_ptr(x, y);
                    float source_r = pixel[0];
                    float source_g = pixel[1];
                    float source_b = pixel[2];
                    float target_r = source_r;
                    float target_g = source_g;
                    float target_b = source_b;

                    if (effect == ComputeEffect::Invert) {
                        target_r = 255.0f - source_r;
                        target_g = 255.0f - source_g;
                        target_b = 255.0f - source_b;
                    } else if (effect == ComputeEffect::AnimatedGradient) {
                        float wave = 0.5f + 0.5f * std::sin(
                                                       (nx * 2.8f + ny * 1.7f -
                                                        params.time_seconds * 0.22f) *
                                                       tau);
                        float band = 0.5f + 0.5f * std::sin(
                                                       (nx * 0.9f - ny * 2.3f +
                                                        params.time_seconds * 0.13f) *
                                                       tau);
                        target_r = 18.0f + 122.0f * wave + 62.0f * ny;
                        target_g = 28.0f + 112.0f * (1.0f - wave) + 74.0f * nx;
                        target_b = 68.0f + 154.0f * band;
                    } else if (effect == ComputeEffect::Mandelbulb) {
                        Float3 color = render_mandelbulb(
                            x, y, frame.width, frame.height, params.time_seconds);
                        target_r = color.x;
                        target_g = color.y;
                        target_b = color.z;
                    }

                    pixel[0] = to_byte(source_r + (target_r - source_r) * strength);
                    pixel[1] = to_byte(source_g + (target_g - source_g) * strength);
                    pixel[2] = to_byte(source_b + (target_b - source_b) * strength);
                    if (effect == ComputeEffect::AnimatedGradient ||
                        effect == ComputeEffect::Mandelbulb) {
                        pixel[3] = 255;
                    }
                }
            }
        }

        bool query_backend(ComputeBackend backend,
                           std::string &backend_name,
                           std::string &device_name,
                           bool &hardware_accelerated,
                           std::string &error) {
            switch (backend) {
            case ComputeBackend::WebGpu:
#ifdef PANIM_HAVE_WEBGPU
            {
                std::string api_name;
                if (!detail::webgpu_backend_available(
                        device_name,
                        api_name,
                        hardware_accelerated,
                        error)) {
                    return false;
                }
                backend_name = "WebGPU / " + api_name;
                return true;
            }
#else
                error = "WebGPU backend was not compiled";
                return false;
#endif
            case ComputeBackend::Cpu:
                backend_name = "CPU";
                device_name = "Portable CPU fallback";
                hardware_accelerated = false;
                return true;
            }
            error = "Unknown compute backend";
            return false;
        }

        bool apply_backend(ComputeBackend backend,
                           Frame &frame,
                           ComputeEffect effect,
                           const ComputeParams &params,
                           std::string &error) {
            switch (backend) {
            case ComputeBackend::WebGpu:
#ifdef PANIM_HAVE_WEBGPU
                return detail::webgpu_backend_apply(frame, effect, params, error);
#else
                error = "WebGPU backend was not compiled";
                return false;
#endif
            case ComputeBackend::Cpu:
                apply_cpu(frame, effect, params);
                return true;
            }
            error = "Unknown compute backend";
            return false;
        }

        ComputeBackend parse_backend(const char *value,
                                     bool &automatic,
                                     bool &valid) {
            automatic = true;
            valid = true;
            if (!value || !value[0])
                return ComputeBackend::WebGpu;

            std::string name(value);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (name == "auto")
                return ComputeBackend::WebGpu;

            automatic = false;
            if (name == "webgpu" || name == "gpu")
                return ComputeBackend::WebGpu;
            if (name == "cpu")
                return ComputeBackend::Cpu;

            valid = false;
            automatic = true;
            return ComputeBackend::WebGpu;
        }

        void initialize_runtime() {
            ComputeRuntime &state = runtime();
            if (state.initialized)
                return;
            state.initialized = true;

            bool automatic = true;
            bool valid = true;
            const char *requested_name = std::getenv("PANIM_COMPUTE_BACKEND");
            ComputeBackend requested =
                parse_backend(requested_name, automatic, valid);
            if (!valid) {
                PANIM_LOG_WARN(
                    "Unknown PANIM_COMPUTE_BACKEND '{}'; using automatic selection",
                    requested_name);
            }

            auto select = [&](ComputeBackend backend) {
                std::string backend_name;
                std::string device_name;
                std::string error;
                bool hardware_accelerated = false;
                if (!query_backend(
                        backend,
                        backend_name,
                        device_name,
                        hardware_accelerated,
                        error)) {
                    return false;
                }
                state.info.backend = backend;
                state.info.backend_name = std::move(backend_name);
                state.info.device_name = std::move(device_name);
                state.info.hardware_accelerated = hardware_accelerated;
                return true;
            };

            bool selected = automatic ? select(ComputeBackend::WebGpu)
                                      : select(requested);
            if (!automatic && !selected) {
                PANIM_LOG_WARN("Requested compute backend '{}' is unavailable",
                               compute_backend_name(requested));
            }

            if (!selected)
                select(ComputeBackend::Cpu);

            PANIM_LOG_INFO("Compute backend: {} ({})",
                           state.info.backend_name,
                           state.info.device_name);
        }

    } // namespace

    const char *compute_backend_name(ComputeBackend backend) {
        switch (backend) {
        case ComputeBackend::Cpu:
            return "CPU";
        case ComputeBackend::WebGpu:
            return "WebGPU";
        }
        return "Unknown";
    }

    const ComputeDeviceInfo &compute_device() {
        initialize_runtime();
        return runtime().info;
    }

    ComputeResult apply_compute_effect(Frame &frame,
                                       ComputeEffect effect,
                                       const ComputeParams &params) {
        initialize_runtime();
        ComputeRuntime &state = runtime();
        if (!state.info.hardware_accelerated && !params.allow_cpu_fallback) {
            return {false,
                    ComputeBackend::Cpu,
                    false,
                    "No hardware compute backend is available"};
        }

        std::string error;
        if (apply_backend(state.info.backend, frame, effect, params, error)) {
            return {true,
                    state.info.backend,
                    state.info.hardware_accelerated,
                    {}};
        }

        if (!state.failure_logged) {
            PANIM_LOG_WARN("{} compute failed: {}",
                           state.info.backend_name,
                           error);
            state.failure_logged = true;
        }
        if (!params.allow_cpu_fallback) {
            return {false,
                    state.info.backend,
                    state.info.hardware_accelerated,
                    error};
        }

        apply_cpu(frame, effect, params);
        state.info = {ComputeBackend::Cpu,
                      "CPU",
                      "Portable CPU fallback after backend failure",
                      false};
        return {true,
                ComputeBackend::Cpu,
                false,
                "Hardware backend failed; used CPU fallback: " + error};
    }

} // namespace panim
