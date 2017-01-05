/*******************************
Copyright (c) 2016-2017 Gr�goire Angerand

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
**********************************/

#include "DeferredRenderer.h"
#include <yave/commands/CmdBufferRecorder.h>

#include <y/io/File.h>

#include <random>

namespace yave {

static constexpr vk::Format depth_format = vk::Format::eD32Sfloat;
static constexpr vk::Format diffuse_format = vk::Format::eR8G8B8A8Unorm;
static constexpr vk::Format normal_format = vk::Format::eR8G8B8A8Unorm;


struct Light {
	math::Vec3 color;
	float radius;
	math::Vec3 positon;
	float padding;
};

static ComputeShader create_shader(DevicePtr dptr) {
	return ComputeShader(dptr, SpirVData::from_file(io::File::open("deferred.comp.spv")));
}

static auto create_lights(DevicePtr dptr, usize dir_count, usize pts_count) {
	using Vec4u32 = math::Vec<4, u32>;
	Buffer<BufferUsage::StorageBit, MemoryFlags::CpuVisible> buffer(dptr, sizeof(Vec4u32) + (dir_count + pts_count) * sizeof(Light));

	std::mt19937 gen(3);
	std::uniform_real_distribution<float> distr(0, 1);
	std::uniform_real_distribution<float> pos_distr(-1, 1);

	core::Vector<Light> lights;
	for(usize i = 0; i != dir_count; i++) {
		lights << Light {
				math::Vec3(distr(gen), distr(gen), distr(gen)), 0,
				math::Vec3(pos_distr(gen), pos_distr(gen), pos_distr(gen)).normalized(), 0
			};
	}
	for(usize i = 0; i != pts_count; i++) {
		lights << Light {
				math::Vec3(distr(gen), distr(gen), distr(gen)), 4,
				math::Vec3(pos_distr(gen), pos_distr(gen), pos_distr(gen)).normalized(), 0
			};
	}

	{
		TypedSubBuffer<Vec4u32, BufferUsage::StorageBit, MemoryFlags::CpuVisible>(buffer, 0, 1).map()[0] = Vec4u32(dir_count, dir_count + pts_count, 0, 0);
	}
	{
		TypedSubBuffer<Light, BufferUsage::StorageBit, MemoryFlags::CpuVisible> light_buffer(buffer, sizeof(Vec4u32), lights.size());
		auto map = light_buffer.map();
		std::copy(lights.begin(), lights.end(), map.begin());
	}

	return buffer;
}

static math::Vec3 extract_position(const math::Matrix4<>& view_matrix) {
	math::Vec3 pos;
	for(usize i = 0; i != 3; i++) {
		pos -= view_matrix[i].sub(3) * view_matrix[i].w();
	}
	return pos;
}

DeferredRenderer::DeferredRenderer(SceneView &scene, const math::Vec2ui& size) :
		DeviceLinked(scene.device()),
		_scene(scene),
		_size(size),
		_depth(device(), depth_format, _size),
		_diffuse(device(), diffuse_format, _size),
		_normal(device(), normal_format, _size),
		_gbuffer(device(), _depth, {_diffuse, _normal}),
		_shader(create_shader(device())),
		_program(_shader),
		_lights(create_lights(device(), 0, 2)),
		_camera(device(), 1),
		_input_set(device(), {Binding(_depth), Binding(_diffuse), Binding(_normal), Binding(_camera)}),
		_lights_set(device(), {Binding(_lights)}) {

	for(usize i = 0; i != 3; i++) {
		if(_size[i] % _shader.local_size()[i]) {
			log_msg("Compute local size at index "_s + i + " does not divide output buffer size.", LogType::Warning);
		}
	}
}

void DeferredRenderer::update() {
	auto view = _scene.view_matrix().transposed();
	auto inverse = (_scene.proj_matrix().transposed() * view).inverse();
	_camera.map()[0] = Camera{inverse.transposed(), extract_position(view)};
}

void DeferredRenderer::draw(CmdBufferRecorder& recorder, const OutputView& out) {
	recorder.bind_framebuffer(_gbuffer);
	_scene.draw(recorder);

#warning barrier needed ?
	//recorder.image_barriers({_depth, _diffuse, _normal}, PipelineStage::AttachmentOutBit, PipelineStage::ComputeBit);


	recorder.dispatch(_program, math::Vec3ui(_size / _shader.local_size().sub(3), 1), {_input_set, _lights_set, create_output_set(out)});
}

const DescriptorSet& DeferredRenderer::create_output_set(const OutputView& out) {
	if(out.size() != _size) {
		fatal("Invalid output image size.");
	}

	auto it = _output_sets.find(out.vk_image_view());
	if(it == _output_sets.end()) {
		it = _output_sets.insert(std::make_pair(out.vk_image_view(), DescriptorSet(device(), {
				Binding(out)
			}))).first;
	}
	return it->second;
}

}
