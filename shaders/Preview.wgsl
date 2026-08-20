struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

struct PreviewParams {
    progress: f32,
    playing: f32,
    hovered_control: f32,
    padding: f32,
};

@group(0) @binding(0) var frame_texture: texture_2d<f32>;
@group(0) @binding(1) var frame_sampler: sampler;
@group(0) @binding(2) var<uniform> params: PreviewParams;

@vertex
fn vertex_main(@builtin(vertex_index) index: u32) -> VertexOutput {
    let positions = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(3.0, -1.0),
        vec2<f32>(-1.0, 3.0)
    );
    var output: VertexOutput;
    let position = positions[index];
    output.position = vec4<f32>(position, 0.0, 1.0);
    output.uv = position * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5);
    return output;
}

fn inside_box(point: vec2<f32>, center: vec2<f32>, half_size: vec2<f32>) -> bool {
    let distance = abs(point - center);
    return distance.x <= half_size.x && distance.y <= half_size.y;
}

fn cross_2d(a: vec2<f32>, b: vec2<f32>) -> f32 {
    return a.x * b.y - a.y * b.x;
}

fn inside_triangle(
    point: vec2<f32>,
    a: vec2<f32>,
    b: vec2<f32>,
    c: vec2<f32>
) -> bool {
    let side_a = cross_2d(b - a, point - a);
    let side_b = cross_2d(c - b, point - b);
    let side_c = cross_2d(a - c, point - c);
    let has_negative = side_a < 0.0 || side_b < 0.0 || side_c < 0.0;
    let has_positive = side_a > 0.0 || side_b > 0.0 || side_c > 0.0;
    return !(has_negative && has_positive);
}

fn is_hovered(control: f32) -> bool {
    return abs(params.hovered_control - control) < 0.25;
}

@fragment
fn fragment_main(input: VertexOutput) -> @location(0) vec4<f32> {
    let frame = textureSample(frame_texture, frame_sampler, input.uv);
    if (input.uv.y < 0.86) {
        return frame;
    }

    let toolbar = vec3<f32>(0.025, 0.035, 0.055);
    let button = vec3<f32>(0.10, 0.13, 0.18);
    let hover = vec3<f32>(0.18, 0.24, 0.32);
    let icon = vec3<f32>(0.92, 0.95, 1.0);
    let muted = vec3<f32>(0.20, 0.25, 0.33);
    let paused = vec3<f32>(0.98, 0.70, 0.24);
    let running = vec3<f32>(0.16, 0.82, 0.78);
    let active_color = select(paused, running, params.playing > 0.5);
    var color = mix(frame.rgb, toolbar, 0.94);

    let centers = array<f32, 6>(0.050, 0.115, 0.185, 0.255, 0.855, 0.925);
    var control = 0u;
    loop {
        if (control >= 6u) {
            break;
        }
        let control_id = f32(control + 1u);
        if (inside_box(input.uv,
                       vec2<f32>(centers[control], 0.93),
                       vec2<f32>(0.027, 0.048))) {
            color = select(button, hover, is_hovered(control_id));
        }
        control += 1u;
    }

    let restart_local = vec2<f32>((input.uv.x - 0.050) / 0.022,
                                  (input.uv.y - 0.93) / 0.038);
    let restart_bar = abs(restart_local.x + 0.52) < 0.10 &&
                      abs(restart_local.y) < 0.52;
    let restart_triangle = inside_triangle(restart_local,
                                           vec2<f32>(-0.28, -0.55),
                                           vec2<f32>(0.58, 0.0),
                                           vec2<f32>(-0.28, 0.55));
    if (restart_bar || restart_triangle) {
        color = icon;
    }

    let previous_local = vec2<f32>((input.uv.x - 0.115) / 0.021,
                                   (input.uv.y - 0.93) / 0.038);
    let previous_triangle = inside_triangle(previous_local,
                                            vec2<f32>(0.48, -0.56),
                                            vec2<f32>(-0.48, 0.0),
                                            vec2<f32>(0.48, 0.56));
    if (previous_triangle) {
        color = icon;
    }

    let play_local = vec2<f32>((input.uv.x - 0.185) / 0.022,
                               (input.uv.y - 0.93) / 0.038);
    let play_triangle = inside_triangle(play_local,
                                        vec2<f32>(-0.38, -0.62),
                                        vec2<f32>(0.58, 0.0),
                                        vec2<f32>(-0.38, 0.62));
    let pause_bars = (abs(play_local.x - 0.28) < 0.14 ||
                      abs(play_local.x + 0.28) < 0.14) &&
                     abs(play_local.y) < 0.58;
    if (select(play_triangle, pause_bars, params.playing > 0.5)) {
        color = active_color;
    }

    let next_local = vec2<f32>((input.uv.x - 0.255) / 0.021,
                               (input.uv.y - 0.93) / 0.038);
    let next_triangle = inside_triangle(next_local,
                                        vec2<f32>(-0.48, -0.56),
                                        vec2<f32>(0.48, 0.0),
                                        vec2<f32>(-0.48, 0.56));
    if (next_triangle) {
        color = icon;
    }

    let timeline_start = 0.30;
    let timeline_end = 0.80;
    let timeline_position = mix(timeline_start, timeline_end, params.progress);
    if (input.uv.x >= timeline_start && input.uv.x <= timeline_end &&
        abs(input.uv.y - 0.93) < 0.010) {
        color = select(muted, active_color, input.uv.x <= timeline_position);
    }
    let knob = vec2<f32>((input.uv.x - timeline_position) / 0.008,
                         (input.uv.y - 0.93) / 0.014);
    if (length(knob) <= 1.0) {
        color = active_color;
    }

    let camera_local = vec2<f32>((input.uv.x - 0.855) / 0.022,
                                 (input.uv.y - 0.93) / 0.038);
    let camera_outer = abs(camera_local.x) < 0.62 &&
                       abs(camera_local.y - 0.08) < 0.46;
    let camera_inner = abs(camera_local.x) < 0.48 &&
                       abs(camera_local.y - 0.04) < 0.30;
    let camera_top = abs(camera_local.x + 0.22) < 0.25 &&
                     abs(camera_local.y + 0.48) < 0.12;
    let camera_lens = length(vec2<f32>(camera_local.x / 0.30,
                                       camera_local.y / 0.42)) < 0.62;
    if ((camera_outer && !camera_inner) || camera_top || camera_lens) {
        color = icon;
    }

    let reload_local = vec2<f32>((input.uv.x - 0.925) / 0.022,
                                 (input.uv.y - 0.93) / 0.038);
    let reload_radius = length(vec2<f32>(reload_local.x,
                                         reload_local.y * 0.80));
    let reload_ring = reload_radius > 0.38 && reload_radius < 0.59 &&
                      !(reload_local.x > 0.20 && reload_local.y < -0.20);
    let reload_arrow = inside_triangle(reload_local,
                                        vec2<f32>(0.02, -0.58),
                                        vec2<f32>(0.68, -0.68),
                                        vec2<f32>(0.50, -0.05));
    if (reload_ring || reload_arrow) {
        color = icon;
    }

    return vec4<f32>(color, 1.0);
}
