#version 450

layout(location = 0) in vec3 in_position;

layout(binding = 0) uniform Uniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
	vec4 tint;
	vec4 edge_color;
	float vertex_color_weight;
} ubo;

void main() {
	vec4 view_position = ubo.view * ubo.model * vec4(in_position, 1.0);

	// Немного сдвигаем рёбра к камере, чтобы они не "дрались" за глубину с гранями
	view_position.z += 0.005;

	gl_Position = ubo.projection * view_position;
}
