#version 450

layout(binding = 0) uniform Uniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
	vec4 tint;
	vec4 edge_color;
	float vertex_color_weight;
} ubo;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = vec4(ubo.edge_color.rgb, 1.0);
}
