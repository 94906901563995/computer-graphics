#version 450

struct PointLight {
	vec3 position;
	float intensity;
	vec3 color;
};

struct SpotLight {
	vec3 position;
	float intensity;
	vec3 color;
	float inner_cos; // косинус внутреннего угла (полная яркость)
	vec3 direction;
	float outer_cos; // косинус внешнего угла (яркость спадает до нуля)
};

layout(std140, set = 0, binding = 0) uniform SceneUniforms {
	mat4 view_projection;
	vec3 camera_position;
	vec3 directional_light_direction;
	vec3 directional_light_color;
	vec3 ambient_light;
	uint point_light_count;
	uint spot_light_count;
} scene;

layout(std430, set = 0, binding = 1) readonly buffer PointLights {
	PointLight point_lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer SpotLights {
	SpotLight spot_lights[];
};

layout(std140, set = 1, binding = 0) uniform ModelUniforms {
	mat4 model;
	vec3 albedo;
	vec3 specular;
	float shininess;
} model_uniforms;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

// Модель Блинн-Фонга: C = I * (cos θ * Kd + Ks * (cos φ)^α),
// где cos θ = n·l, cos φ = n·h, h – половинный вектор между l и v
vec3 blinnPhong(vec3 n, vec3 v, vec3 l, vec3 radiance) {
	const float n_dot_l = max(dot(n, l), 0.0);
	// Источник с обратной стороны не даёт ни диффузного света, ни блика.
	if (n_dot_l == 0.0) {
		return vec3(0.0);
	}

	const vec3 h = normalize(l + v);
	// Чем больше shininess, тем уже блик вокруг направления h.
	const float n_dot_h = max(dot(n, h), 0.0);

	return radiance * (model_uniforms.albedo * n_dot_l +
	                   model_uniforms.specular * pow(n_dot_h, model_uniforms.shininess));
}

void main() {
	const vec3 n = normalize(in_normal);
	const vec3 v = normalize(scene.camera_position - in_position);

	vec3 color = scene.ambient_light * model_uniforms.albedo;

	// Меняем знак: храним направление лучей, а формуле нужен вектор к источнику.
	color += blinnPhong(n, v, -scene.directional_light_direction, scene.directional_light_color);

	for (uint i = 0; i < scene.point_light_count; ++i) {
		const PointLight light = point_lights[i];

		const vec3 to_light = light.position - in_position;
		const float distance_squared = dot(to_light, to_light);
		const vec3 l = to_light * inversesqrt(distance_squared);

		// Затухание по закону обратных квадратов: 1 / D^2
		const vec3 radiance = light.color * light.intensity / distance_squared;

		color += blinnPhong(n, v, l, radiance);
	}

	for (uint i = 0; i < scene.spot_light_count; ++i) {
		const SpotLight light = spot_lights[i];

		const vec3 to_light = light.position - in_position;
		const float distance_squared = dot(to_light, to_light);
		const vec3 l = to_light * inversesqrt(distance_squared);

		// Мягкий край: между внешним и внутренним углом яркость линейно
		// растёт от 0 до 1 по косинусу угла между лучом и осью прожектора
		const float cos_theta = dot(-l, light.direction);
		const float cone = clamp((cos_theta - light.outer_cos) /
		                         max(light.inner_cos - light.outer_cos, 1e-4), 0.0, 1.0);

		const vec3 radiance = light.color * light.intensity * cone / distance_squared;

		color += blinnPhong(n, v, l, radiance);
	}

	out_color = vec4(color, 1.0);
}
