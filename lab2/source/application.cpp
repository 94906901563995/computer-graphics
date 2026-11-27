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
Vec3 operator-(Vec3 a) { return { -a.x, -a.y, -a.z }; }
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

Vec3 toVec3(const float v[3]) {
	return { v[0], v[1], v[2] };
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

// Глубина Vulkan лежит в [0; 1], а ось Y в NDC направлена вниз,
// поэтому Y отражается, чтобы в мире ось Y смотрела вверх
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

// Направление взгляда для углов рыскания (вокруг Y) и тангажа (вокруг X):
// d = Ry(yaw) * Rx(pitch) * (0, 0, -1)
Vec3 direction(float yaw, float pitch) {
	const float cy = std::cos(yaw), sy = std::sin(yaw);
	const float cp = std::cos(pitch), sp = std::sin(pitch);
	return { -sy * cp, sp, -cy * cp };
}

// ---------------------------------------------------------------------------
// Геометрия: вершины с позицией и нормалью
// ---------------------------------------------------------------------------

struct Vertex {
	Vec3 position;
	Vec3 normal;
};

struct MeshData {
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;
};

// Правильный додекаэдр с плоскими гранями: у каждой грани свои 5 вершин
// с нормалью грани, поэтому освещение внутри грани не интерполирует нормали соседей
MeshData makeDodecahedron() {
	const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
	const float inv_phi = 1.0f / phi;
	const float signs[] = { -1.0f, 1.0f };

	// 20 вершин: (±1, ±1, ±1), (0, ±1/φ, ±φ), (±1/φ, ±φ, 0), (±φ, 0, ±1/φ),
	// нормированные на радиус описанной сферы √3
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
	for (Vec3& p : points) {
		p = p * (1.0f / std::sqrt(3.0f));
	}

	// Нормали 12 граней направлены в вершины двойственного икосаэдра:
	// (0, ±φ, ±1), (±1, 0, ±φ), (±φ, ±1, 0)
	std::vector<Vec3> face_normals;
	for (float a : signs) {
		for (float b : signs) {
			face_normals.push_back(normalize({ 0.0f, a * phi, b }));
			face_normals.push_back(normalize({ a, 0.0f, b * phi }));
			face_normals.push_back(normalize({ a * phi, b, 0.0f }));
		}
	}

	MeshData mesh;

	for (Vec3 n : face_normals) {
		// Грань образуют 5 вершин, наиболее удалённых вдоль нормали
		float max_distance = -1.0f;
		for (Vec3 p : points) {
			max_distance = std::max(max_distance, dot(p, n));
		}

		std::vector<Vec3> face;
		for (Vec3 p : points) {
			if (dot(p, n) > max_distance - 1e-4f) {
				face.push_back(p);
			}
		}

		// Упорядочиваем вершины против часовой стрелки, если смотреть на грань снаружи
		const Vec3 center = n * max_distance;
		const Vec3 u = normalize(face[0] - center);
		const Vec3 w = cross(n, u);

		auto angle = [&](Vec3 p) {
			const Vec3 d = p - center;
			return std::atan2(dot(d, w), dot(d, u));
		};

		std::sort(face.begin(), face.end(), [&](Vec3 a, Vec3 b) {
			return angle(a) < angle(b);
		});

		const uint16_t base = uint16_t(mesh.vertices.size());
		for (Vec3 p : face) {
			mesh.vertices.push_back({ p, n });
		}

		// Пятиугольник разбивается веером на 3 треугольника
		for (uint16_t i = 1; i + 1 < uint16_t(face.size()); ++i) {
			mesh.indices.insert(mesh.indices.end(), {
				base, uint16_t(base + i), uint16_t(base + i + 1)
			});
		}
	}

	return mesh;
}

// Квадрат в плоскости XZ со стороной 2 и нормалью +Y
MeshData makePlane() {
	MeshData mesh;

	const Vec3 up = { 0.0f, 1.0f, 0.0f };
	mesh.vertices = {
		{ { -1.0f, 0.0f, -1.0f }, up },
		{ {  1.0f, 0.0f, -1.0f }, up },
		{ { -1.0f, 0.0f,  1.0f }, up },
		{ {  1.0f, 0.0f,  1.0f }, up },
	};

	// Против часовой стрелки при взгляде сверху
	mesh.indices = { 0, 2, 1, 1, 2, 3 };

	return mesh;
}

// ---------------------------------------------------------------------------
// Ресурсы Vulkan
// ---------------------------------------------------------------------------

// set = 0, binding = 0 (std140)
struct SceneUniforms {
	Mat4 view_projection;
	Vec3 camera_position;
	float padding0;
	Vec3 directional_light_direction;
	float padding1;
	Vec3 directional_light_color;
	float padding2;
	Vec3 ambient_light;
	uint32_t point_light_count;
	uint32_t spot_light_count;
};

// set = 0, binding = 1 (std430)
struct PointLight {
	Vec3 position;
	float intensity;
	Vec3 color;
	float padding;
};

// set = 0, binding = 2 (std430)
struct SpotLight {
	Vec3 position;
	float intensity;
	Vec3 color;
	float inner_cos;
	Vec3 direction;
	float outer_cos;
};

// set = 1, binding = 0 (std140)
struct ModelUniforms {
	Mat4 model;
	Vec3 albedo;
	float padding;
	Vec3 specular;
	float shininess;
};

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* mapped = nullptr;
};

// Storage-буфер, который пересоздаётся с большей ёмкостью, когда элементы не помещаются
struct StorageArray {
	Buffer buffer;
	size_t capacity = 0;
};

struct Mesh {
	Buffer vertices;
	Buffer indices;
	uint32_t index_count;
};

enum MeshKind {
	mesh_dodecahedron,
	mesh_plane,
	mesh_count,
};

Mesh meshes[mesh_count];

Buffer scene_uniform_buffer;
StorageArray point_light_buffer;
StorageArray spot_light_buffer;

VkDescriptorSetLayout vk_scene_set_layout;
VkDescriptorSetLayout vk_model_set_layout;
VkDescriptorPool vk_descriptor_pool;
VkDescriptorSet vk_scene_set;

VkPipelineLayout vk_pipeline_layout;
VkPipeline vk_pipeline;

constexpr uint32_t max_models = 16;

// ---------------------------------------------------------------------------
// Состояние сцены, изменяемое через интерфейс
// ---------------------------------------------------------------------------

struct Camera {
	float position[3] = { 0.0f, 2.5f, 7.5f };
	float yaw = 0.0f;
	float pitch = -18.0f;
	float fov = 60.0f;
	float z_near = 0.1f;
	float z_far = 100.0f;
	float speed = 4.0f;
};

struct DirectionalLightState {
	bool enabled = true;
	float yaw = 35.0f;
	float pitch = -50.0f;
	float color[3] = { 1.0f, 0.95f, 0.9f };
	float intensity = 0.25f;
};

struct PointLightState {
	float position[3];
	float color[3];
	float intensity;
};

struct SpotLightState {
	float position[3];
	float color[3];
	float intensity;
	float yaw;
	float pitch;
	float inner_angle;
	float outer_angle;
};

struct Model {
	const char* name;
	MeshKind mesh;
	float position[3];
	float rotation[3];
	float scale[3];
	float spin; // скорость вращения вокруг Y, градусов в секунду
	float albedo[3];
	float specular[3];
	float shininess;

	float spin_angle = 0.0f;
	Buffer uniform_buffer;
	VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
};

Camera camera;
DirectionalLightState directional_light;
float ambient_color[3] = { 0.6f, 0.7f, 1.0f };
float ambient_intensity = 0.08f;

std::vector<PointLightState> point_lights = {
	{ { 2.2f, 1.2f, 1.8f }, { 1.0f, 0.6f, 0.25f }, 6.0f },
	{ { -2.5f, 0.8f, -1.0f }, { 0.3f, 0.5f, 1.0f }, 5.0f },
};

std::vector<SpotLightState> spot_lights = {
	{ { 0.0f, 5.0f, 3.5f }, { 1.0f, 1.0f, 1.0f }, 25.0f, 0.0f, -55.0f, 14.0f, 24.0f },
};

std::vector<Model> models = {
	{ "Додекаэдр (золото)", mesh_dodecahedron, { 0.0f, 0.2f, 0.0f }, { 0.0f, 0.0f, 0.0f },
	  { 1.2f, 1.2f, 1.2f }, 20.0f, { 0.85f, 0.6f, 0.2f }, { 0.9f, 0.8f, 0.6f }, 64.0f },
	{ "Додекаэдр (матовый)", mesh_dodecahedron, { -2.8f, -0.45f, 0.6f }, { 20.0f, 0.0f, 10.0f },
	  { 0.75f, 0.75f, 0.75f }, -15.0f, { 0.2f, 0.35f, 0.8f }, { 0.05f, 0.05f, 0.05f }, 4.0f },
	{ "Додекаэдр (глянцевый)", mesh_dodecahedron, { 2.8f, -0.45f, -0.6f }, { 0.0f, 0.0f, 30.0f },
	  { 0.75f, 0.75f, 0.75f }, 30.0f, { 0.15f, 0.6f, 0.25f }, { 1.0f, 1.0f, 1.0f }, 256.0f },
	{ "Пол", mesh_plane, { 0.0f, -1.2f, 0.0f }, { 0.0f, 0.0f, 0.0f },
	  { 30.0f, 1.0f, 30.0f }, 0.0f, { 0.55f, 0.55f, 0.55f }, { 0.2f, 0.2f, 0.2f }, 16.0f },
};

double last_time;
SceneUniforms scene_uniforms;

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

bool createMesh(const MeshData& data, Mesh& mesh) {
	const size_t vertices_size = data.vertices.size() * sizeof(Vertex);
	const size_t indices_size = data.indices.size() * sizeof(uint16_t);

	if (!createBuffer(vertices_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, mesh.vertices) ||
	    !createBuffer(indices_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, mesh.indices)) {
		return false;
	}

	uploadBuffer(mesh.vertices, data.vertices.data(), vertices_size);
	uploadBuffer(mesh.indices, data.indices.data(), indices_size);
	mesh.index_count = uint32_t(data.indices.size());

	return true;
}

// Гарантирует, что в storage-буфер поместятся count элементов размера stride.
// Возвращает false при ошибке; reallocated = true, если буфер был пересоздан
bool reserveStorage(StorageArray& array, size_t count, size_t stride, bool& reallocated) {
	reallocated = false;

	if (array.buffer.buffer != VK_NULL_HANDLE && count <= array.capacity) {
		return true;
	}

	size_t capacity = std::max<size_t>(array.capacity, 4);
	// Запас ёмкости позволяет добавлять свет без пересоздания буфера каждый раз.
	while (capacity < count) {
		capacity *= 2;
	}

	if (array.buffer.buffer != VK_NULL_HANDLE) {
		destroyBuffer(array.buffer);
	}

	if (!createBuffer(capacity * stride, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, array.buffer)) {
		array.capacity = 0;
		return false;
	}

	array.capacity = capacity;
	reallocated = true;

	return true;
}

void writeSceneDescriptors() {
	auto& context = graphics::internal::context;

	const VkDescriptorBufferInfo buffers[] = {
		{ .buffer = scene_uniform_buffer.buffer, .offset = 0, .range = sizeof(SceneUniforms) },
		{ .buffer = point_light_buffer.buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE },
		{ .buffer = spot_light_buffer.buffer.buffer, .offset = 0, .range = VK_WHOLE_SIZE },
	};

	const VkDescriptorType types[] = {
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	};

	VkWriteDescriptorSet writes[3];
	for (uint32_t i = 0; i < 3; ++i) {
		writes[i] = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = vk_scene_set,
			.dstBinding = i,
			.descriptorCount = 1,
			.descriptorType = types[i],
			.pBufferInfo = &buffers[i],
		};
	}

	vkUpdateDescriptorSets(context.device, 3, writes, 0, nullptr);
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

VkPipeline createPipeline(const char* vertex_shader_path, const char* fragment_shader_path) {
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
				.offset = offsetof(Vertex, normal),
			},
		};

		const VkPipelineVertexInputStateCreateInfo vertex_input = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 1,
			.pVertexBindingDescriptions = &binding,
			.vertexAttributeDescriptionCount = sizeof(attributes) / sizeof(attributes[0]),
			.pVertexAttributeDescriptions = attributes,
		};

		const VkPipelineInputAssemblyStateCreateInfo input_assembly = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		};

		const VkPipelineViewportStateCreateInfo viewport = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		};

		// Треугольники заданы против часовой стрелки при взгляде снаружи,
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

// Свободная камера: ПКМ + мышь – поворот, WASD – движение, Q/E – вниз/вверх
void updateCameraInput(float delta_time) {
	const ImGuiIO& io = ImGui::GetIO();

	if (!io.WantCaptureMouse && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		camera.yaw -= io.MouseDelta.x * 0.2f;
		camera.pitch -= io.MouseDelta.y * 0.2f;
		camera.pitch = std::clamp(camera.pitch, -89.0f, 89.0f);
	}

	if (camera.yaw > 180.0f) camera.yaw -= 360.0f;
	if (camera.yaw < -180.0f) camera.yaw += 360.0f;

	if (io.WantCaptureKeyboard) {
		return;
	}

	const Vec3 forward = direction(radians(camera.yaw), radians(camera.pitch));
	const Vec3 right = normalize(cross(forward, { 0.0f, 1.0f, 0.0f }));
	const Vec3 up = { 0.0f, 1.0f, 0.0f };

	Vec3 move = { 0.0f, 0.0f, 0.0f };
	if (ImGui::IsKeyDown(ImGuiKey_W)) move = move + forward;
	if (ImGui::IsKeyDown(ImGuiKey_S)) move = move - forward;
	if (ImGui::IsKeyDown(ImGuiKey_D)) move = move + right;
	if (ImGui::IsKeyDown(ImGuiKey_A)) move = move - right;
	if (ImGui::IsKeyDown(ImGuiKey_E)) move = move + up;
	if (ImGui::IsKeyDown(ImGuiKey_Q)) move = move - up;

	const Vec3 position = toVec3(camera.position) + move * (camera.speed * delta_time);
	camera.position[0] = position.x;
	camera.position[1] = position.y;
	camera.position[2] = position.z;
}

void drawInterface() {
	ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(430.0f, 860.0f), ImGuiCond_FirstUseEver);

	ImGui::Begin("Камера и освещение");
	ImGui::Text("%.0f FPS", double(ImGui::GetIO().Framerate));
	ImGui::TextDisabled("ПКМ + мышь – поворот, WASD/QE – движение");

	if (ImGui::CollapsingHeader("Камера", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat3("Позиция##camera", camera.position, 0.05f);
		ImGui::SliderFloat("Рыскание", &camera.yaw, -180.0f, 180.0f, "%.0f°");
		ImGui::SliderFloat("Тангаж", &camera.pitch, -89.0f, 89.0f, "%.0f°");
		ImGui::SliderFloat("Угол обзора", &camera.fov, 10.0f, 150.0f, "%.0f°");
		ImGui::SliderFloat("Ближняя плоскость", &camera.z_near, 0.01f, 2.0f);
		ImGui::SliderFloat("Дальняя плоскость", &camera.z_far, 10.0f, 300.0f);
		ImGui::SliderFloat("Скорость", &camera.speed, 0.5f, 20.0f);
	}

	if (ImGui::CollapsingHeader("Рассеянный и направленный свет", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Цвет рассеянного", ambient_color);
		ImGui::SliderFloat("Интенсивность рассеянного", &ambient_intensity, 0.0f, 1.0f);

		ImGui::Separator();
		ImGui::Checkbox("Направленный источник", &directional_light.enabled);
		ImGui::SliderFloat("Азимут##directional", &directional_light.yaw, -180.0f, 180.0f, "%.0f°");
		ImGui::SliderFloat("Наклон##directional", &directional_light.pitch, -90.0f, 90.0f, "%.0f°");
		ImGui::ColorEdit3("Цвет##directional", directional_light.color);
		ImGui::SliderFloat("Интенсивность##directional", &directional_light.intensity, 0.0f, 3.0f);
	}

	if (ImGui::CollapsingHeader("Точечные источники", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button("Добавить точечный")) {
			point_lights.push_back({ { 0.0f, 1.5f, 2.0f }, { 1.0f, 1.0f, 1.0f }, 5.0f });
		}

		for (size_t i = 0; i < point_lights.size(); ++i) {
			PointLightState& light = point_lights[i];
			ImGui::PushID(int(i));

			const bool open = ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen,
			                                    "Точечный #%zu", i + 1);
			ImGui::SameLine();
			const bool remove = ImGui::SmallButton("Удалить");

			if (open) {
				ImGui::DragFloat3("Позиция", light.position, 0.05f);
				ImGui::ColorEdit3("Цвет", light.color);
				ImGui::SliderFloat("Интенсивность", &light.intensity, 0.0f, 50.0f);
				ImGui::TreePop();
			}

			ImGui::PopID();

			if (remove) {
				point_lights.erase(point_lights.begin() + std::ptrdiff_t(i));
				--i;
			}
		}
	}

	if (ImGui::CollapsingHeader("Прожекторы", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::Button("Добавить прожектор")) {
			spot_lights.push_back({ { 0.0f, 4.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 30.0f,
			                        0.0f, -90.0f, 15.0f, 25.0f });
		}

		for (size_t i = 0; i < spot_lights.size(); ++i) {
			SpotLightState& light = spot_lights[i];
			ImGui::PushID(int(i + 1000));

			const bool open = ImGui::TreeNodeEx("light", ImGuiTreeNodeFlags_DefaultOpen,
			                                    "Прожектор #%zu", i + 1);
			ImGui::SameLine();
			const bool remove = ImGui::SmallButton("Удалить");

			if (open) {
				ImGui::DragFloat3("Позиция", light.position, 0.05f);
				ImGui::SliderFloat("Азимут", &light.yaw, -180.0f, 180.0f, "%.0f°");
				ImGui::SliderFloat("Наклон", &light.pitch, -90.0f, 90.0f, "%.0f°");
				ImGui::ColorEdit3("Цвет", light.color);
				ImGui::SliderFloat("Интенсивность", &light.intensity, 0.0f, 200.0f);
				ImGui::SliderFloat("Внутренний угол", &light.inner_angle, 0.0f, 89.0f, "%.0f°");
				ImGui::SliderFloat("Внешний угол", &light.outer_angle, 0.0f, 89.0f, "%.0f°");
				// Внешний угол не может быть меньше внутреннего
				light.outer_angle = std::max(light.outer_angle, light.inner_angle);
				ImGui::TreePop();
			}

			ImGui::PopID();

			if (remove) {
				spot_lights.erase(spot_lights.begin() + std::ptrdiff_t(i));
				--i;
			}
		}
	}

	if (ImGui::CollapsingHeader("Объекты и материалы")) {
		for (size_t i = 0; i < models.size(); ++i) {
			Model& model = models[i];
			ImGui::PushID(int(i + 2000));

			if (ImGui::TreeNode(model.name)) {
				ImGui::DragFloat3("Позиция", model.position, 0.05f);
				ImGui::SliderFloat3("Поворот XYZ", model.rotation, -180.0f, 180.0f, "%.0f°");
				ImGui::DragFloat3("Растяжение", model.scale, 0.01f, 0.05f, 20.0f);
				ImGui::SliderFloat("Вращение, °/с", &model.spin, -180.0f, 180.0f, "%.0f");
				ImGui::ColorEdit3("Альбедо", model.albedo);
				ImGui::ColorEdit3("Цвет блика", model.specular);
				ImGui::SliderFloat("Коэффициент блика", &model.shininess, 1.0f, 512.0f, "%.0f",
				                   ImGuiSliderFlags_Logarithmic);
				ImGui::TreePop();
			}

			ImGui::PopID();
		}
	}

	ImGui::End();
}

} // namespace

bool initialize() {
	auto& context = graphics::internal::context;

	loadCyrillicFont();

	if (!createMesh(makeDodecahedron(), meshes[mesh_dodecahedron]) ||
	    !createMesh(makePlane(), meshes[mesh_plane])) {
		return false;
	}

	bool reallocated;
	if (!createBuffer(sizeof(SceneUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, scene_uniform_buffer) ||
	    !reserveStorage(point_light_buffer, point_lights.size(), sizeof(PointLight), reallocated) ||
	    !reserveStorage(spot_light_buffer, spot_lights.size(), sizeof(SpotLight), reallocated)) {
		return false;
	}

	// set = 0: общие данные сцены – uniform-буфер и два storage-буфера источников света
	const VkDescriptorSetLayoutBinding scene_bindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
		{
			.binding = 2,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
		},
	};

	// set = 1: данные модели – матрица трансформации и параметры материала
	const VkDescriptorSetLayoutBinding model_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	};

	const VkDescriptorSetLayoutCreateInfo scene_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = sizeof(scene_bindings) / sizeof(scene_bindings[0]),
		.pBindings = scene_bindings,
	};

	const VkDescriptorSetLayoutCreateInfo model_set_layout = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &model_binding,
	};

	if (vkCreateDescriptorSetLayout(context.device, &scene_set_layout, nullptr,
	                                &vk_scene_set_layout) != VK_SUCCESS ||
	    vkCreateDescriptorSetLayout(context.device, &model_set_layout, nullptr,
	                                &vk_model_set_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor set layouts\n";
		return false;
	}

	const VkDescriptorPoolSize pool_sizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1 + max_models },
		{ .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2 },
	};

	const VkDescriptorPoolCreateInfo pool = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1 + max_models,
		.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]),
		.pPoolSizes = pool_sizes,
	};

	if (vkCreateDescriptorPool(context.device, &pool, nullptr, &vk_descriptor_pool) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan descriptor pool\n";
		return false;
	}

	const VkDescriptorSetAllocateInfo scene_set_allocate = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vk_descriptor_pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vk_scene_set_layout,
	};

	if (vkAllocateDescriptorSets(context.device, &scene_set_allocate, &vk_scene_set) != VK_SUCCESS) {
		std::cerr << "Failed to allocate Vulkan descriptor set for scene\n";
		return false;
	}

	writeSceneDescriptors();

	// Каждая модель получает свой uniform-буфер и свой набор дескрипторов (set = 1)
	for (Model& model : models) {
		if (!createBuffer(sizeof(ModelUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		                  model.uniform_buffer)) {
			return false;
		}

		const VkDescriptorSetAllocateInfo model_set_allocate = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = vk_descriptor_pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &vk_model_set_layout,
		};

		if (vkAllocateDescriptorSets(context.device, &model_set_allocate,
		                             &model.descriptor_set) != VK_SUCCESS) {
			std::cerr << "Failed to allocate Vulkan descriptor set for model\n";
			return false;
		}

		const VkDescriptorBufferInfo buffer_info = {
			.buffer = model.uniform_buffer.buffer,
			.offset = 0,
			.range = sizeof(ModelUniforms),
		};

		const VkWriteDescriptorSet write = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = model.descriptor_set,
			.dstBinding = 0,
			.descriptorCount = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			.pBufferInfo = &buffer_info,
		};

		vkUpdateDescriptorSets(context.device, 1, &write, 0, nullptr);
	}

	const VkDescriptorSetLayout set_layouts[] = {
		vk_scene_set_layout,
		vk_model_set_layout,
	};

	const VkPipelineLayoutCreateInfo pipeline_layout = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = sizeof(set_layouts) / sizeof(set_layouts[0]),
		.pSetLayouts = set_layouts,
	};

	if (vkCreatePipelineLayout(context.device, &pipeline_layout, nullptr,
	                           &vk_pipeline_layout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	vk_pipeline = createPipeline("shaders/lit.vert.spv", "shaders/lit.frag.spv");

	return vk_pipeline != VK_NULL_HANDLE;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);

	vkDestroyPipeline(context.device, vk_pipeline, nullptr);
	vkDestroyPipelineLayout(context.device, vk_pipeline_layout, nullptr);

	vkDestroyDescriptorPool(context.device, vk_descriptor_pool, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_model_set_layout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, vk_scene_set_layout, nullptr);

	for (Model& model : models) {
		destroyBuffer(model.uniform_buffer);
	}

	destroyBuffer(spot_light_buffer.buffer);
	destroyBuffer(point_light_buffer.buffer);
	destroyBuffer(scene_uniform_buffer);

	for (Mesh& mesh : meshes) {
		destroyBuffer(mesh.indices);
		destroyBuffer(mesh.vertices);
	}
}

void update(double time) {
	auto& context = graphics::internal::context;

	const float delta_time = float(time - last_time);
	last_time = time;

	updateCameraInput(delta_time);
	drawInterface();

	for (Model& model : models) {
		model.spin_angle = std::fmod(model.spin_angle + model.spin * delta_time, 360.0f);
	}

	// Матрица вида – обратная матрице модели камеры C = T(p) * Ry(yaw) * Rx(pitch):
	// V = C^-1 = Rx(-pitch) * Ry(-yaw) * T(-p)
	const Vec3 camera_position = toVec3(camera.position);
	const Mat4 view = rotationX(-radians(camera.pitch)) *
	                  rotationY(-radians(camera.yaw)) *
	                  translation(-camera_position);

	const float aspect = float(context.swapchain_extent.width) /
	                     float(context.swapchain_extent.height);
	const Mat4 projection = perspective(radians(camera.fov), aspect, camera.z_near, camera.z_far);

	scene_uniforms.view_projection = projection * view;
	scene_uniforms.camera_position = camera_position;
	scene_uniforms.directional_light_direction =
		direction(radians(directional_light.yaw), radians(directional_light.pitch));
	scene_uniforms.directional_light_color = directional_light.enabled
		? toVec3(directional_light.color) * directional_light.intensity
		: Vec3{ 0.0f, 0.0f, 0.0f };
	scene_uniforms.ambient_light = toVec3(ambient_color) * ambient_intensity;
	scene_uniforms.point_light_count = uint32_t(point_lights.size());
	scene_uniforms.spot_light_count = uint32_t(spot_lights.size());
}

void render(const graphics::internal::FrameData& fd) {
	auto& context = graphics::internal::context;

	if (fd.command_buffer == VK_NULL_HANDLE) {
		return;
	}

	// prepare() уже дождался завершения предыдущего кадра, поэтому буферы
	// можно безопасно перезаписывать и пересоздавать здесь
	bool point_reallocated = false, spot_reallocated = false;
	if (!reserveStorage(point_light_buffer, point_lights.size(), sizeof(PointLight), point_reallocated) ||
	    !reserveStorage(spot_light_buffer, spot_lights.size(), sizeof(SpotLight), spot_reallocated)) {
		return;
	}
	if (point_reallocated || spot_reallocated) {
		// Новый VkBuffer нужно заново связать с дескриптором сцены.
		writeSceneDescriptors();
	}

	uploadBuffer(scene_uniform_buffer, &scene_uniforms, sizeof(scene_uniforms));

	std::vector<PointLight> point_data;
	for (const PointLightState& light : point_lights) {
		point_data.push_back({
			.position = toVec3(light.position),
			.intensity = light.intensity,
			.color = toVec3(light.color),
		});
	}

	std::vector<SpotLight> spot_data;
	for (const SpotLightState& light : spot_lights) {
		spot_data.push_back({
			.position = toVec3(light.position),
			.intensity = light.intensity,
			.color = toVec3(light.color),
			.inner_cos = std::cos(radians(light.inner_angle)),
			.direction = direction(radians(light.yaw), radians(light.pitch)),
			.outer_cos = std::cos(radians(light.outer_angle)),
		});
	}

	if (!point_data.empty()) {
		uploadBuffer(point_light_buffer.buffer, point_data.data(), point_data.size() * sizeof(PointLight));
	}
	if (!spot_data.empty()) {
		uploadBuffer(spot_light_buffer.buffer, spot_data.data(), spot_data.size() * sizeof(SpotLight));
	}

	// Модельная матрица: M = T * Rz * Ry * Rx * S
	for (Model& model : models) {
		const ModelUniforms uniforms = {
			.model = translation(toVec3(model.position)) *
			         rotationZ(radians(model.rotation[2])) *
			         rotationY(radians(model.rotation[1] + model.spin_angle)) *
			         rotationX(radians(model.rotation[0])) *
			         scaling(toVec3(model.scale)),
			.albedo = toVec3(model.albedo),
			.specular = toVec3(model.specular),
			.shininess = model.shininess,
		};

		uploadBuffer(model.uniform_buffer, &uniforms, sizeof(uniforms));
	}

	const VkCommandBuffer cb = fd.command_buffer;

	vkResetCommandBuffer(cb, 0);

	const VkCommandBufferBeginInfo begin = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};

	vkBeginCommandBuffer(cb, &begin);

	const VkClearValue clear_values[] = {
		{ .color = { { 0.01f, 0.012f, 0.02f, 1.0f } } },
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

	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

	// Набор сцены привязывается один раз, набор модели – перед каждым объектом
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
	                        0, 1, &vk_scene_set, 0, nullptr);

	for (const Model& model : models) {
		const Mesh& mesh = meshes[model.mesh];
		// Сетка может быть общей, но матрица и материал у каждого объекта свои.
		const VkDeviceSize offset = 0;

		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline_layout,
		                        1, 1, &model.descriptor_set, 0, nullptr);
		vkCmdBindVertexBuffers(cb, 0, 1, &mesh.vertices.buffer, &offset);
		vkCmdBindIndexBuffer(cb, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT16);
		vkCmdDrawIndexed(cb, mesh.index_count, 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(cb);

	vkEndCommandBuffer(cb);
}

} // namespace application
