#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	vec3 directional_light_direction;
	vec3 directional_light_color;
	vec3 ambient_light;
	uint point_light_count;
	uint spot_light_count;
} scene;

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	vec3 albedo;
	vec3 specular;
	float shininess;
} model_uniforms;

layout(location = 0) out vec3 out_position;
layout(location = 1) out vec3 out_normal;

void main() {
	const vec4 world_position = model_uniforms.model * vec4(in_position, 1.0);

	out_position = world_position.xyz;
	// Свет и камера тоже заданы в мире: все направления считаются в одной системе.
	// Нормаль преобразуется обратной транспонированной матрицей, чтобы
	// оставаться перпендикулярной поверхности при неравномерном растяжении
	out_normal = transpose(inverse(mat3(model_uniforms.model))) * in_normal;

	gl_Position = scene.view_projection * world_position;
}
