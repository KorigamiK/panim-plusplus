struct PanimParams {
    width: u32,
    height: u32,
    effect: u32,
    time_seconds: f32,
    strength: f32,
    _padding_0: u32,
    _padding_1: u32,
    _padding_2: u32,
};

@group(0) @binding(0)
var<storage, read_write> pixels: array<u32>;

@group(0) @binding(1)
var<uniform> params: PanimParams;

fn to_byte(value: f32) -> u32 {
    return u32(clamp(value, 0.0, 255.0));
}

fn mandelbulb_distance(point: vec3<f32>) -> f32 {
    var z = point;
    var derivative = 1.0;
    var radius = 0.0;

    for (var iteration = 0u; iteration < 7u; iteration += 1u) {
        radius = length(z);
        if (radius > 2.35) {
            break;
        }

        let safe_radius = max(radius, 0.00001);
        let theta = acos(clamp(z.z / safe_radius, -1.0, 1.0));
        let phi = atan2(z.y, z.x);
        let radial_power = pow(safe_radius, 7.0);
        derivative = 8.0 * radial_power * derivative + 1.0;

        let powered_radius = radial_power * safe_radius;
        let powered_theta = theta * 8.0;
        let powered_phi = phi * 8.0;
        z = powered_radius * vec3<f32>(
            sin(powered_theta) * cos(powered_phi),
            sin(powered_theta) * sin(powered_phi),
            cos(powered_theta)) + point;
    }

    let safe_radius = max(radius, 0.00001);
    return 0.5 * log(safe_radius) * safe_radius / max(derivative, 0.00001);
}

fn mandelbulb_normal(point: vec3<f32>) -> vec3<f32> {
    let epsilon = 0.0025;
    let x = vec3<f32>(epsilon, 0.0, 0.0);
    let y = vec3<f32>(0.0, epsilon, 0.0);
    let z = vec3<f32>(0.0, 0.0, epsilon);
    return normalize(vec3<f32>(
        mandelbulb_distance(point + x) - mandelbulb_distance(point - x),
        mandelbulb_distance(point + y) - mandelbulb_distance(point - y),
        mandelbulb_distance(point + z) - mandelbulb_distance(point - z)));
}

fn render_mandelbulb(pixel: vec2<u32>) -> vec3<f32> {
    let resolution = vec2<f32>(f32(params.width), f32(params.height));
    let screen = (vec2<f32>(pixel) + vec2<f32>(0.5)) / resolution;
    let aspect = resolution.x / max(resolution.y, 1.0);
    let uv = vec2<f32>(
        (screen.x * 2.0 - 1.0) * aspect,
        1.0 - screen.y * 2.0);

    let orbit = params.time_seconds * 0.32 + 0.5;
    let camera = vec3<f32>(
        2.85 * cos(orbit),
        0.65 + 0.18 * sin(orbit * 0.7),
        2.85 * sin(orbit));
    let focal_point = vec3<f32>(0.0, 0.02, 0.0);
    let forward = normalize(focal_point - camera);
    let right = normalize(cross(forward, vec3<f32>(0.0, 1.0, 0.0)));
    let up = cross(right, forward);
    let ray_direction = normalize(
        forward * 1.75 + right * uv.x + up * uv.y);

    var travel = 0.0;
    var hit = false;
    var step_index = 0u;
    for (var step = 0u; step < 52u; step += 1u) {
        step_index = step;
        let point = camera + ray_direction * travel;
        let distance = mandelbulb_distance(point);
        if (distance < 0.0025) {
            hit = true;
            break;
        }
        travel += max(distance * 0.78, 0.001);
        if (travel > 6.5) {
            break;
        }
    }

    let horizon = clamp(0.5 + 0.5 * ray_direction.y, 0.0, 1.0);
    var color = mix(
        vec3<f32>(7.0, 10.0, 24.0),
        vec3<f32>(32.0, 42.0, 78.0),
        horizon);
    let view_glow = pow(
        max(dot(ray_direction, normalize(-camera)), 0.0), 12.0);
    color += vec3<f32>(32.0, 20.0, 56.0) * view_glow;

    if (hit) {
        let point = camera + ray_direction * travel;
        let normal = mandelbulb_normal(point);
        let light = normalize(vec3<f32>(-0.45, 0.8, 0.35));
        let diffuse = max(dot(normal, light), 0.0);
        let facing = max(dot(normal, -ray_direction), 0.0);
        let rim = pow(1.0 - facing, 2.4);
        let detail = 0.5 + 0.5 * cos(
            3.3 * point.y + 1.7 * point.x - params.time_seconds * 0.18);
        let base = mix(
            vec3<f32>(40.0, 105.0, 210.0),
            vec3<f32>(235.0, 92.0, 182.0),
            detail);
        let occlusion = 1.0 - 0.42 * f32(step_index) / 52.0;
        color = base * (0.13 + diffuse * 1.05) * occlusion;
        color += vec3<f32>(90.0, 180.0, 255.0) * rim * 0.72;
        color += vec3<f32>(255.0, 220.0, 178.0) * pow(diffuse, 14.0) * 0.5;
    }

    return color;
}

@compute @workgroup_size(8, 8)
fn panim_effect(@builtin(global_invocation_id) global_id: vec3<u32>) {
    if (global_id.x >= params.width || global_id.y >= params.height) {
        return;
    }

    let index = global_id.y * params.width + global_id.x;
    let packed = pixels[index];
    let source = vec3<f32>(
        f32(packed & 0xffu),
        f32((packed >> 8u) & 0xffu),
        f32((packed >> 16u) & 0xffu));
    var target_color = source;
    var alpha = (packed >> 24u) & 0xffu;

    if (params.effect == 0u) {
        target_color = vec3<f32>(255.0) - source;
    } else if (params.effect == 1u) {
        let x = index % params.width;
        let y = index / params.width;
        let nx = select(0.0, f32(x) / f32(params.width - 1u), params.width > 1u);
        let ny = select(0.0, f32(y) / f32(params.height - 1u), params.height > 1u);
        let tau = 6.28318530718;
        let wave = 0.5 + 0.5 * sin(
            (nx * 2.8 + ny * 1.7 - params.time_seconds * 0.22) * tau);
        let band = 0.5 + 0.5 * sin(
            (nx * 0.9 - ny * 2.3 + params.time_seconds * 0.13) * tau);
        target_color = vec3<f32>(
            18.0 + 122.0 * wave + 62.0 * ny,
            28.0 + 112.0 * (1.0 - wave) + 74.0 * nx,
            68.0 + 154.0 * band);
        alpha = 255u;
    } else if (params.effect == 2u) {
        target_color = render_mandelbulb(global_id.xy);
        alpha = 255u;
    }

    let amount = clamp(params.strength, 0.0, 1.0);
    let result = mix(source, target_color, amount);
    pixels[index] = to_byte(result.r) |
        (to_byte(result.g) << 8u) |
        (to_byte(result.b) << 16u) |
        (alpha << 24u);
}
