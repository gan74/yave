#version 450

layout(set = 0, binding = 0) uniform ViewProj {
	mat4 matrix;
} view_proj;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_tangent;
layout(location = 3) in vec2 in_uv;

layout(location = 8) in mat4 in_model;
layout(location = 13) in uint in_id;

layout(location = 0) out uint instance_id;

void main() {
	mat3 model = mat3(in_model);
	gl_Position = view_proj.matrix * in_model * vec4(in_position, 1.0);

	instance_id = in_id;
}
