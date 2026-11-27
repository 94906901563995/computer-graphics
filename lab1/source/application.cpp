#include "application.hpp"

#include <cmath>
#include <cstring>
#include <cstdint>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

#include <imgui.h>

namespace application {

namespace {

// ---------------------------------------------------------------------------
// Математика: векторы и матрицы 4x4 (хранение по столбцам, как в GLSL)
// ---------------------------------------------------------------------------

constexpr float pi = 3.14159265358979323846f;

float radians(float degrees) {
	return degrees * pi / 180.0f;
}

struct Vec3 {
	float x, y, z;
};

Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

float dot(Vec3 a, Vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x,
	};
}

Vec3 normalize(Vec3 v) {
	return v * (1.0f / std::sqrt(dot(v, v)));
}

// m[столбец][строка]
struct Mat4 {
	float m[4][4];
};

Mat4 identity() {
	Mat4 r = {};
	for (int i = 0; i < 4; ++i) {
		r.m[i][i] = 1.0f;
	}
	return r;
}

Mat4 operator*(const Mat4& a, const Mat4& b) {
	Mat4 r = {};
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			for (int k = 0; k < 4; ++k) {
				r.m[column][row] += a.m[k][row] * b.m[column][k];
			}
		}
	}
	return r;
}

Mat4 translation(Vec3 t) {
	Mat4 r = identity();
	r.m[3][0] = t.x;
	r.m[3][1] = t.y;
	r.m[3][2] = t.z;
	return r;
}

Mat4 scaling(Vec3 s) {
	Mat4 r = identity();
	r.m[0][0] = s.x;
	r.m[1][1] = s.y;
	r.m[2][2] = s.z;
	return r;
}

Mat4 rotationX(float angle) {
	const float c = std::cos(angle), s = std::sin(angle);
	Mat4 r = identity();
	r.m[1][1] = c;  r.m[1][2] = s;
	r.m[2][1] = -s; r.m[2][2] = c;
	return r;
}

Mat4 rotationY(float angle) {
	const float c = std::cos(angle), s = std::sin(angle);
	Mat4 r = identity();
	r.m[0][0] = c; r.m[0][2] = -s;
	r.m[2][0] = s; r.m[2][2] = c;
	return r;
}

Mat4 rotationZ(float angle) {
	const float c = std::cos(angle), s = std::sin(angle);
	Mat4 r = identity();
	r.m[0][0] = c;  r.m[0][1] = s;
	r.m[1][0] = -s; r.m[1][1] = c;
	return r;
}

// Правая система координат: камера смотрит вдоль -Z
Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
	const Vec3 f = normalize(target - eye);
	const Vec3 s = normalize(cross(f, up));
	const Vec3 u = cross(s, f);

	Mat4 r = identity();
	r.m[0][0] = s.x;  r.m[1][0] = s.y;  r.m[2][0] = s.z;
	r.m[0][1] = u.x;  r.m[1][1] = u.y;  r.m[2][1] = u.z;
	r.m[0][2] = -f.x; r.m[1][2] = -f.y; r.m[2][2] = -f.z;
	r.m[3][0] = -dot(s, eye);
	r.m[3][1] = -dot(u, eye);
	r.m[3][2] = dot(f, eye);
	return r;
}

// Глубина Vulkan лежит в [0; 1], а ось Y в NDC направлена вниз, поэтому
// в обеих матрицах проекции Y отражается, чтобы в мире ось Y смотрела вверх
Mat4 perspective(float fov_y, float aspect, float z_near, float z_far) {
	const float f = 1.0f / std::tan(fov_y * 0.5f);

	Mat4 r = {};
	r.m[0][0] = f / aspect;
	r.m[1][1] = -f;
	r.m[2][2] = z_far / (z_near - z_far);
	r.m[2][3] = -1.0f;
	r.m[3][2] = z_near * z_far / (z_near - z_far);
	return r;
}

Mat4 orthographic(float left, float right, float bottom, float top, float z_near, float z_far) {
	Mat4 r = identity();
	r.m[0][0] = 2.0f / (right - left);
	r.m[1][1] = -2.0f / (top - bottom);
	r.m[2][2] = 1.0f / (z_near - z_far);
	r.m[3][0] = -(right + left) / (right - left);
	r.m[3][1] = (top + bottom) / (top - bottom);
	r.m[3][2] = z_near / (z_near - z_far);
	return r;
}

// ---------------------------------------------------------------------------
// Геометрия: правильный додекаэдр
// ---------------------------------------------------------------------------

struct Vertex {
	Vec3 position;
	Vec3 color;
};

struct Mesh {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> triangles; // VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
	std::vector<uint16_t> edges;     // VK_PRIMITIVE_TOPOLOGY_LINE_LIST
};

Mesh makeDodecahedron() {
	const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
	const float inv_phi = 1.0f / phi;
	const float signs[] = { -1.0f, 1.0f };

	// 20 вершин: (±1, ±1, ±1), (0, ±1/φ, ±φ), (±1/φ, ±φ, 0), (±φ, 0, ±1/φ)
	std::vector<Vec3> points;
	for (float x : signs) {
		for (float y : signs) {
			for (float z : signs) {
				points.push_back({ x, y, z });
			}
		}
	}
	for (float a : signs) {
		for (float b : signs) {
			points.push_back({ 0.0f, a * inv_phi, b * phi });
			points.push_back({ a * inv_phi, b * phi, 0.0f });
			points.push_back({ a * phi, 0.0f, b * inv_phi });
		}
	}

	Mesh mesh;

	// Нормируем на радиус описанной сферы (√3), чтобы фигура вписывалась в единичную сферу.
	// Цвет вершины задаётся процедурно из её позиции в локальной системе координат:
	// направление из центра [-1; 1]^3 отображается в RGB [0; 1]^3
	for (Vec3 p : points) {
		const Vec3 position = p * (1.0f / std::sqrt(3.0f));
		const Vec3 color = normalize(position) * 0.5f + Vec3{ 0.5f, 0.5f, 0.5f };
		mesh.vertices.push_back({ position, color });
	}

	// Нормали 12 пятиугольных граней направлены в вершины двойственного икосаэдра:
	// (0, ±φ, ±1), (±1, 0, ±φ), (±φ, ±1, 0)
	std::vector<Vec3> face_normals;
	for (float a : signs) {
		for (float b : signs) {
			face_normals.push_back(normalize({ 0.0f, a * phi, b }));
			face_normals.push_back(normalize({ a, 0.0f, b * phi }));
			face_normals.push_back(normalize({ a * phi, b, 0.0f }));
		}
	}

	for (Vec3 n : face_normals) {
		// Грань образуют 5 вершин, наиболее удалённых вдоль нормали
		float max_distance = -1.0f;
		for (const Vertex& v : mesh.vertices) {
			max_distance = std::max(max_distance, dot(v.position, n));
		}

		std::vector<uint16_t> face;
		for (size_t i = 0; i < mesh.vertices.size(); ++i) {
			if (dot(mesh.vertices[i].position, n) > max_distance - 1e-4f) {
				face.push_back(uint16_t(i));
			}
		}

		// Упорядочиваем вершины против часовой стрелки, если смотреть на грань снаружи
		const Vec3 center = n * max_distance;
		const Vec3 u = normalize(mesh.vertices[face[0]].position - center);
		const Vec3 w = cross(n, u);

		auto angle = [&](uint16_t i) {
			const Vec3 d = mesh.vertices[i].position - center;
			return std::atan2(dot(d, w), dot(d, u));
		};

		std::sort(face.begin(), face.end(), [&](uint16_t a, uint16_t b) {
			return angle(a) < angle(b);
		});

		// Пятиугольник разбивается веером на 3 треугольника
		for (size_t i = 1; i + 1 < face.size(); ++i) {
			mesh.triangles.insert(mesh.triangles.end(), { face[0], face[i], face[i + 1] });
		}

		// Каждое ребро принадлежит двум граням, добавляем его только один раз
		for (size_t i = 0; i < face.size(); ++i) {
			const uint16_t a = face[i];
			const uint16_t b = face[(i + 1) % face.size()];
			if (a < b) {
				mesh.edges.insert(mesh.edges.end(), { a, b });
			}
		}
	}

	return mesh;
}

// ---------------------------------------------------------------------------
// Ресурсы Vulkan
// ---------------------------------------------------------------------------

// Раскладка совпадает с uniform-блоком в шейдерах (std140)
struct Uniforms {
	Mat4 model;
	Mat4 view;
	Mat4 projection;
	float tint[4];
	float edge_color[4];
	float vertex_color_weight;
	float padding[3];
};

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

Buffer vertex_buffer;
Buffer triangle_index_buffer;
Buffer edge_index_buffer;
Buffer uniform_buffer;

uint32_t triangle_index_count;
uint32_t edge_index_count;

VkDescriptorSetLayout vk_descriptor_set_layout;
VkDescriptorPool vk_descriptor_pool;
VkDescriptorSet vk_descriptor_set;

VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline_shape;
VkPipeline vk_pipeline_edges;

// ---------------------------------------------------------------------------
// Состояние сцены, изменяемое через интерфейс
// ---------------------------------------------------------------------------

enum Projection : int {
	projection_perspective,
	projection_orthographic,
};

struct State {
	int projection = projection_perspective;
	float fov = 60.0f;
	float ortho_half_height = 3.5f;
	float z_near = 0.1f;
	float z_far = 100.0f;

	float camera_distance = 8.0f;
	float camera_elevation = 25.0f;

	float position[3] = { 0.0f, 0.0f, 0.0f };
	float rotation[3] = { 0.0f, 0.0f, 0.0f };
	float scale[3] = { 1.0f, 1.0f, 1.0f };

	bool animate = true;
	bool playing = true;
	float speed = 1.0f;
	float orbit_radius = 2.0f;
	float epicycle_radius = 0.6f;
	int epicycle_frequency = 5;
	float height_amplitude = 0.5f;
	int height_frequency = 2;
	float spin[3] = { 40.0f, 70.0f, 0.0f };

	float tint[3] = { 1.0f, 1.0f, 1.0f };
	float vertex_color_weight = 1.0f;
	bool show_edges = true;
	float edge_color[3] = { 0.05f, 0.05f, 0.05f };
};

State state;
double last_time;
float animation_time;

Uniforms uniforms;

// ---------------------------------------------------------------------------

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	auto& context = graphics::internal::context;

	const VkBufferCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	// Память, видимая и GPU, и CPU, постоянно отображённая в адресное пространство процесса
	const VmaAllocationCreateInfo allocation = {
		.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		         VMA_ALLOCATION_CREATE_MAPPED_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo allocation_info;

	if (vmaCreateBuffer(context.allocator, &info, &allocation, &buffer.buffer,
	                    &buffer.allocation, &allocation_info) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan buffer\n";
		return false;
	}

	buffer.mapped = allocation_info.pMappedData;

	return true;
}

void uploadBuffer(const Buffer& buffer, const void* data, size_t size) {
	auto& context = graphics::internal::context;

	std::memcpy(buffer.mapped, data, size);
	vmaFlushAllocation(context.allocator, buffer.allocation, 0, size);
}

void destroyBuffer(Buffer& buffer) {
	auto& context = graphics::internal::context;

	vmaDestroyBuffer(context.allocator, buffer.buffer, buffer.allocation);
	buffer = {};
}

VkShaderModule loadShaderModule(const char* path) {
	auto& context = graphics::internal::context;

	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file) {
		std::cerr << "Failed to open shader file " << path << '\n';
		return VK_NULL_HANDLE;
	}

	const size_t size = size_t(file.tellg());
	std::vector<uint32_t> code((size + 3) / 4);

	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), std::streamsize(size));

	const VkShaderModuleCreateInfo info = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code.data(),
	};

	VkShaderModule module;

	if (vkCreateShaderModule(context.device, &info, nullptr, &module) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan shader module from " << path << '\n';
		return VK_NULL_HANDLE;
	}

	return module;
}

// attribute_count: сколько атрибутов вершины читает шейдер (1 – только позиция, 2 – позиция и цвет)
VkPipeline createPipeline(const char* vertex_shader_path, const char* fragment_shader_path,
                          VkPrimitiveTopology topology, uint32_t attribute_count) {
	auto& context = graphics::internal::context;

	const VkShaderModule vertex_shader = loadShaderModule(vertex_shader_path);
	const VkShaderModule fragment_shader = loadShaderModule(fragment_shader_path);

	VkPipeline pipeline = VK_NULL_HANDLE;

	if (vertex_shader == VK_NULL_HANDLE || fragment_shader == VK_NULL_HANDLE) {
		goto cleanup;
	}

	{
		const VkPipelineShaderStageCreateInfo stages[] = {
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = vertex_shader,
				.pName = "main",
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = fragment_shader,
				.pName = "main",
			},
		};

		const VkVertexInputBindingDescription binding = {
			.binding = 0,
			.stride = sizeof(Vertex),
			.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
		};

		const VkVertexInputAttributeDescription attributes[] = {
			{
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, position),
			},
			{
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(Vertex, color),
			},
		};

		const VkPipelineVertexInputStateCreateInfo vertex_input = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &binding,
			.vertexAttributeDescriptionCount = attribute_count,
			.pVertexAttributeDescriptions = attributes,
		};

		const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = topology,
		};

		const VkPipelineViewportStateCreateInfo viewport = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		// Треугольники граней заданы против часовой стрелки при взгляде снаружи,
		// задние грани отбрасываются
		const VkPipelineRasterizationStateCreateInfo rasterization = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_BACK_BIT,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.lineWidth = 1.0f,
		};

		const VkPipelineMultisampleStateCreateInfo multisample = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		};

		const VkPipelineDepthStencilStateCreateInfo depth_stencil = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = VK_TRUE,
			.depthWriteEnable = VK_TRUE,
			.depthCompareOp = VK_COMPARE_OP_LESS,
		};

		const VkPipelineColorBlendAttachmentState blend_attachment = {
			.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		};

		const VkPipelineColorBlendStateCreateInfo blend = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = &blend_attachment,
		};

		const VkDynamicState dynamic_states[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};

		const VkPipelineDynamicStateCreateInfo dynamic = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
			.pDynamicStates = dynamic_states,
		};

		const VkGraphicsPipelineCreateInfo info = {
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = sizeof(stages) / sizeof(stages[0]),
			.pStages = stages,
			.pVertexInputState = &vertex_input,
			.pInputAssemblyState = &input_assembly,
			.pViewportState = &viewport,
			.pRasterizationState = &rasterization,
			.pMultisampleState = &multisample,
			.pDepthStencilState = &depth_stencil,
			.pColorBlendState = &blend,
			.pDynamicState = &dynamic,
			.layout = vk_pipeline_layout,
			.renderPass = context.render_pass,
			.subpass = 0,
		};

		if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &info, nullptr,
		                              &pipeline) != VK_SUCCESS) {
			std::cerr << "Failed to create Vulkan graphics pipeline\n";
			pipeline = VK_NULL_HANDLE;
		}
	}

cleanup:
	vkDestroyShaderModule(context.device, fragment_shader, nullptr);
	vkDestroyShaderModule(context.device, vertex_shader, nullptr);

	return pipeline;
}

void loadCyrillicFont() {
	const char* const candidates[] = {
		"C:/Windows/Fonts/segoeui.ttf",
		"C:/Windows/Fonts/arial.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	};

	for (const char* path : candidates) {
		if (std::ifstream(path).good()) {
			ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 16.0f);
			return;
		}
	}
}

// Два круговых движения в XZ и синусоида по Y задают сложную траекторию.
Vec3 trajectory(float t) {
	const float k = float(state.epicycle_frequency);
	const float m = float(state.height_frequency);

	return {
		state.orbit_radius * std::cos(t) + state.epicycle_radius * std::cos(k * t),
		state.height_amplitude * std::sin(m * t),
		state.orbit_radius * std::sin(t) + state.epicycle_radius * std::sin(k * t),
	};
}

void drawInterface() {
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(400.0f, 0.0f), ImGuiCond_FirstUseEver);

	ImGui::Begin("Правильный додекаэдр");
	ImGui::Text("%.0f FPS", double(ImGui::GetIO().Framerate));

	if (ImGui::CollapsingHeader("Проекция", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::RadioButton("Перспективная", &state.projection, projection_perspective);
		ImGui::SameLine();
		ImGui::RadioButton("Ортографическая", &state.projection, projection_orthographic);

		if (state.projection == projection_perspective) {
			ImGui::SliderFloat("Угол обзора", &state.fov, 10.0f, 150.0f, "%.0f°");
		} else {
			ImGui::SliderFloat("Полувысота", &state.ortho_half_height, 0.5f, 10.0f);
		}

		ImGui::SliderFloat("Ближняя плоскость", &state.z_near, 0.01f, 5.0f);
		ImGui::SliderFloat("Дальняя плоскость", &state.z_far, 10.0f, 200.0f);
		ImGui::SliderFloat("Расстояние камеры", &state.camera_distance, 2.0f, 30.0f);
		ImGui::SliderFloat("Наклон камеры", &state.camera_elevation, -89.0f, 89.0f, "%.0f°");
	}

	if (ImGui::CollapsingHeader("Трансформация", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Позиция", state.position, 0.01f, -10.0f, 10.0f);
		ImGui::SliderFloat3("Поворот XYZ", state.rotation, -180.0f, 180.0f, "%.0f°");
		ImGui::DragFloat3("Растяжение", state.scale, 0.01f, 0.05f, 5.0f);

		if (ImGui::Button("Сбросить трансформацию")) {
			const State defaults;
			std::copy_n(defaults.position, 3, state.position);
			std::copy_n(defaults.rotation, 3, state.rotation);
			std::copy_n(defaults.scale, 3, state.scale);
		}
	}

	if (ImGui::CollapsingHeader("Анимация", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Движение по траектории", &state.animate);

		if (ImGui::Button(state.playing ? "Пауза" : "Воспроизвести")) {
			state.playing = !state.playing;
		}
		ImGui::SameLine();
		if (ImGui::Button("В начало")) {
			animation_time = 0.0f;
		}

		ImGui::SliderFloat("Скорость", &state.speed, 0.0f, 5.0f);
		ImGui::SliderFloat("Радиус орбиты R", &state.orbit_radius, 0.0f, 5.0f);
		ImGui::SliderFloat("Радиус эпицикла r", &state.epicycle_radius, 0.0f, 2.0f);
		ImGui::SliderInt("Частота эпицикла k", &state.epicycle_frequency, 1, 12);
		ImGui::SliderFloat("Амплитуда по Y", &state.height_amplitude, 0.0f, 3.0f);
		ImGui::SliderInt("Частота по Y", &state.height_frequency, 0, 8);
		ImGui::SliderFloat3("Вращение, °/с", state.spin, -360.0f, 360.0f, "%.0f");
	}

	if (ImGui::CollapsingHeader("Цвет", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Цвет фигуры", state.tint);
		ImGui::SliderFloat("Вклад цветов вершин", &state.vertex_color_weight, 0.0f, 1.0f);
		ImGui::Checkbox("Показывать рёбра", &state.show_edges);
		ImGui::ColorEdit3("Цвет рёбер", state.edge_color);
	}

	ImGui::End();
}

} // namespace

bool initialize() {
	auto& context = graphics::internal::context;

	loadCyrillicFont();

	// Геометрия: вершинный буфер и два индексных буфера (грани и рёбра)
	const Mesh mesh = makeDodecahedron();
	// Одна сетка: отдельные индексы для треугольников граней и линий рёбер.

	const size_t vertices_size = mesh.vertices.size() * sizeof(Vertex);
	const size_t triangles_size = mesh.triangles.size() * sizeof(uint16_t);
	const size_t edges_size = mesh.edges.size() * sizeof(uint16_t);

	if (!createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertex_buffer) ||
	    !createBuffer(triangles_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, triangle_index_buffer) ||
	    !createBuffer(edges_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, edge_index_buffer) ||
	    !createBuffer(sizeof(Uniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, uniform_buffer)) {
		return false;
	}

	uploadBuffer(vertex_buffer, mesh.vertices.data(), vertices_size);
	uploadBuffer(triangle_index_buffer, mesh.triangles.data(), triangles_size);
	uploadBuffer(edge_index_buffer, mesh.edges.data(), edges_size);

	triangle_index_count = uint32_t(mesh.triangles.size());
	edge_index_count = uint32_t(mesh.edges.size());

	// Описание набора дескрипторов: один uniform-буфер в binding = 0
	const VkDescriptorSetLayoutBinding binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &set_layout, nullptr,
	                                &vk_descriptor_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layout\n";
		return false;
	}

	const VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
	};

	const VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};

	if (vkCreateDescriptorPool(context.device, &pool, nullptr, &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	const VkDescriptorSetAllocateInfo set_allocate = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_descriptor_set_layout,
	};

	if (vkAllocateDescriptorSets(context.device, &set_allocate, &vk_descriptor_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set\n";
		return false;
	}

	const VkDescriptorBufferInfo uniform_buffer_info = {
		.buffer = uniform_buffer.buffer,
		.offset = 0,
		.range = sizeof(Uniforms),
	};

	const VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = vk_descriptor_set,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.pBufferInfo = &uniform_buffer_info,
	};

	vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);

	const VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &vk_descriptor_set_layout,
	};

	if (vkCreatePipelineLayout(context.device, &pipeline_layout, nullptr,
	                           &vk_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	vk_pipeline_shape = createPipeline("shaders/shape.vert.spv", "shaders/shape.frag.spv",
	                                   VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 2);
	vk_pipeline_edges = createPipeline("shaders/edge.vert.spv", "shaders/edge.frag.spv",
	                                   VK_PRIMITIVE_TOPOLOGY_LINE_LIST, 1);

	if (vk_pipeline_shape == VK_NULL_HANDLE || vk_pipeline_edges == VK_NULL_HANDLE) {
		return false;
	}

	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, vk_pipeline_edges, nullptr);
	vkDestroyPipeline(context.device, vk_pipeline_shape, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_descriptor_set_layout, nullptr);

	destroyBuffer(uniform_buffer);
	destroyBuffer(edge_index_buffer);
	destroyBuffer(triangle_index_buffer);
	destroyBuffer(vertex_buffer);
}

void update(double time) {
	auto& context = graphics::internal::context;

	const float delta_time = float(time - last_time);
	last_time = time;

	drawInterface();

	if (state.animate && state.playing) {
		// Пауза останавливает время, delta_time убирает зависимость скорости от FPS.
		animation_time += delta_time * state.speed;
	}

	// Модельная матрица: M = T * Rz * Ry * Rx * S
	// К вершине сначала применяется масштаб, затем повороты и перенос.
	Vec3 position = { state.position[0], state.position[1], state.position[2] };
	Vec3 rotation = { state.rotation[0], state.rotation[1], state.rotation[2] };

	if (state.animate) {
		position = position + trajectory(animation_time);
		rotation = rotation + Vec3{ state.spin[0], state.spin[1], state.spin[2] } * animation_time;
	}

	uniforms.model = translation(position) *
	                 rotationZ(radians(rotation.z)) *
	                 rotationY(radians(rotation.y)) *
	                 rotationX(radians(rotation.x)) *
	                 scaling({ state.scale[0], state.scale[1], state.scale[2] });

	// Камера на сфере вокруг начала координат
	const float elevation = radians(state.camera_elevation);
	const Vec3 eye = Vec3{ 0.0f, std::sin(elevation), std::cos(elevation) } * state.camera_distance;

	uniforms.view = lookAt(eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f });

	const float aspect = float(context.swapchain_extent.width) /
	                     float(context.swapchain_extent.height);

	if (state.projection == projection_perspective) {
		uniforms.projection = perspective(radians(state.fov), aspect, state.z_near, state.z_far);
	} else {
		const float h = state.ortho_half_height;
		uniforms.projection = orthographic(-h * aspect, h * aspect, -h, h, state.z_near, state.z_far);
	}

	std::copy_n(state.tint, 3, uniforms.tint);
	uniforms.tint[3] = 1.0f;
	std::copy_n(state.edge_color, 3, uniforms.edge_color);
	uniforms.edge_color[3] = 1.0f;
	uniforms.vertex_color_weight = state.vertex_color_weight;
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	// prepare() уже дождался завершения предыдущего кадра, поэтому
	// uniform-буфер можно безопасно перезаписывать здесь
	uploadBuffer(uniform_buffer, &uniforms, sizeof(uniforms));

	const VkCommandBuffer cb = fd.command_buffer;

	vkResetCommandBuffer(cb, 0);

	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(cb, &begin);

	const VkClearValue clear_values[] = {
		{ .color = { { 0.08f, 0.09f, 0.12f, 1.0f } } },
		{ .depthStencil = { 1.0f, 0 } },
	};

	const VkRenderPassBeginInfo render_pass_begin = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = context.render_pass,
		.framebuffer = fd.framebuffer,
		.renderArea = { .extent = context.swapchain_extent },
		.clearValueCount = sizeof(clear_values) / sizeof(clear_values[0]),
		.pClearValues = clear_values,
	};

	vkCmdBeginRenderPass(cb, &render_pass_begin, VK_SUBPASS_CONTENTS_INLINE);

	const VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = float(context.swapchain_extent.width),
		.height = float(context.swapchain_extent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	const VkRect2D scissor = { .extent = context.swapchain_extent };

	vkCmdSetViewport(cb, 0, 1, &viewport);
	vkCmdSetScissor(cb, 0, 1, &scissor);

	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(cb, 0, 1, &vertex_buffer.buffer, &offset);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        0, 1, &vk_descriptor_set, 0, nullptr);

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_shape);
	vkCmdBindIndexBuffer(cb, triangle_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(cb, triangle_index_count, 1, 0, 0, 0);

	if (state.show_edges) {
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_edges);
		vkCmdBindIndexBuffer(cb, edge_index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cb, edge_index_count, 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(cb);

	vkEndCommandBuffer(cb);
}

} // namespace application
