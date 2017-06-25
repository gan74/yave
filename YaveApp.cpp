/*******************************
Copyright (c) 2016-2017 Grégoire Angerand

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
#include "YaveApp.h"

#include <yave/image/ImageData.h>
#include <yave/buffer/TypedBuffer.h>
#include <yave/renderers/RenderingPipeline.h>

#include <y/io/File.h>

#include <iostream>

namespace yave {

static core::Chrono time;

YaveApp::YaveApp(DebugParams params) : instance(params), device(instance), mesh_pool(&device) {
	log_msg("sizeof(StaticMesh) = "_s + sizeof(StaticMesh));
	log_msg("sizeof(Matrix4) = "_s + sizeof(math::Matrix4<>));

	create_assets();
}

YaveApp::~YaveApp() {
	device.vk_queue(QueueFamily::Graphics).waitIdle();

	delete scene_view;
	delete scene;

	material = decltype(material)();
	mesh_texture = Texture();
	cubemap = Cubemap();

	swapchain = nullptr;
}

void YaveApp::draw() {
	core::DebugTimer f("frame", core::Duration::milliseconds(8));

	FrameToken frame = swapchain->next_frame();

	CmdBufferRecorder<> recorder(device.create_cmd_buffer());

	{
		RenderingPipeline pipeline(frame);
		pipeline.dispatch(renderer, recorder);
	}

	RecordedCmdBuffer<> cmd_buffer(std::move(recorder));

	vk::PipelineStageFlags pipe_stage_flags = vk::PipelineStageFlagBits::eBottomOfPipe;
	auto graphic_queue = device.vk_queue(QueueFamily::Graphics);
	auto vk_buffer = cmd_buffer.vk_cmd_buffer();

	cmd_buffer.wait();
	device.vk_device().resetFences(cmd_buffer.vk_fence());
	graphic_queue.submit(vk::SubmitInfo()
			.setWaitSemaphoreCount(1)
			.setPWaitSemaphores(&frame.image_aquired)
			.setPWaitDstStageMask(&pipe_stage_flags)
			.setCommandBufferCount(1)
			.setPCommandBuffers(&vk_buffer)
			.setSignalSemaphoreCount(1)
			.setPSignalSemaphores(&frame.render_finished),
		cmd_buffer.vk_fence());

	swapchain->present(frame, graphic_queue);

	//graphic_queue.waitIdle();
}

void YaveApp::update(math::Vec2 angles) {
	float dist = 500.0f;

	auto cam_tr = math::rotation(angles.x(), {0, 0, -1}) * math::rotation(angles.y(), {0, 1, 0});
	auto cam_pos = cam_tr * math::Vec4(dist, 0, 0, 1);
	auto cam_up = cam_tr * math::Vec4(0, 0, 1, 0);

	camera.set_view(math::look_at(cam_pos.to<3>() / cam_pos.w(), math::Vec3(), cam_up.to<3>()));
	camera.set_proj(math::perspective(math::to_rad(45), 4.0f / 3.0f, 0.01f,  10000.0f));
}

void YaveApp::create_assets() {
	{
		{
			auto image = ImageData::from_file(io::File::open("../tools/image_to_yt/chalet.yt").expected("Unable to load texture file."));
			mesh_texture = Texture(&device, image);

			auto cube = ImageData::from_file(io::File::open("../tools/image_to_yt/cubemap/sky.yt").expected("Unable to load texture file."));
			cubemap = Cubemap(&device, cube);
		}
		material = AssetPtr<Material>(Material(&device, MaterialData()
				.set_frag_data(SpirVData::from_file(io::File::open("basic.frag.spv").expected("Unable to load spirv file.")))
				.set_vert_data(SpirVData::from_file(io::File::open("basic.vert.spv").expected("Unable to load spirv file.")))
				//.set_bindings({Binding(TextureView(mesh_texture))})
			));
	}

	core::Vector<const char*> meshes = {"../tools/mesh_to_ym/SK_Mannequin.ym"};
	core::Vector<core::Unique<StaticMesh>> objects;
	core::Vector<core::Unique<Renderable>> renderables;
	core::Vector<core::Unique<Light>> lights;

	{
		Light l(Light::Directional);
		l.set_basis(math::Vec3{1.0f, 1.0f, 3.0f}.normalized(), {1.0f, 0.0f, 0.0f});
		l.color() = math::Vec3{1.0f};
		lights << std::move(l);

		l.set_basis(-math::Vec3{1.0f, 1.0f, 3.0f}.normalized(), {1.0f, 0.0f, 0.0f});
		l.color() = math::Vec3{0.5f};
		lights << std::move(l);
	}

	/*{
		auto image = ImageData::from_file(io::File::open("../tools/image_to_yt/height.png.yt").expected("Unable to load texture file."));
		renderables << new HeightmapTerrain(mesh_pool, AssetPtr<Texture>(Texture(&device, image)));
	}*/

	for(auto name : meshes) {
		auto m_data = MeshData::from_file(io::File::open(name).expected("Unable to load mesh file"));
		log_msg(core::str(m_data.triangles.size()) + " triangles loaded (" + core::str(m_data.vertices.size()) + " vertices)");
		auto mesh = AssetPtr<StaticMeshInstance>(std::move(mesh_pool.create_static_mesh(m_data).expected("Unable to allocate static mesh.")));

		auto obj = StaticMesh(mesh, material);
		obj.position() = math::Vec3{0, 0, 0};
		objects << std::move(obj);
	}


	scene = new Scene(std::move(objects), std::move(renderables), std::move(lights));
	scene_view = new SceneView(*scene, camera);

	update();

}


void YaveApp::create_renderers() {
	auto culling = core::Arc<CullingNode>(new CullingNode(*scene_view));
	auto gbuffer = core::Arc<GBufferRenderer>(new GBufferRenderer(&device, swapchain->size(), culling));
	//auto depth = core::Arc<BufferRenderer>(new DepthRenderer(&device, swapchain->size(), culling));
	auto deferred = core::Arc<BufferRenderer>(new DeferredRenderer(gbuffer));
	renderer = core::Arc<EndOfPipeline>(new ColorCorrectionRenderer(deferred));
}

}

