#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(binding = 0) uniform Uniforms {
	mat4 model;
	mat4 view;
	mat4 projection;
	vec4 tint;
	vec4 edge_color;
	float vertex_color_weight;
} ubo;

layout(location = 0) out vec3 out_color;

void main() {
	// Из локальных координат идём в мир, затем в камеру и проекцию.
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(in_position, 1.0);

	// Процедурный цвет вершины смешивается с белым, итог умножается на цвет из интерфейса
	out_color = mix(vec3(1.0), in_color, ubo.vertex_color_weight) * ubo.tint.rgb;
}
