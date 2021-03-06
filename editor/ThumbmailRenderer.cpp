/*******************************
Copyright (c) 2016-2021 Grégoire Angerand

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

#include "ThumbmailRenderer.h"
#include "EditorResources.h"

#include <yave/assets/AssetLoader.h>
#include <yave/material/Material.h>
#include <yave/material/SimpleMaterialData.h>
#include <yave/meshes/StaticMesh.h>
#include <yave/meshes/MeshData.h>
#include <yave/graphics/images/ImageData.h>
#include <yave/graphics/device/DeviceResources.h>
#include <yave/graphics/commands/CmdBufferRecorder.h>
#include <yave/graphics/descriptors/DescriptorSet.h>

#include <yave/renderer/DefaultRenderer.h>
#include <yave/framegraph/FrameGraph.h>
#include <yave/framegraph/FrameGraphPass.h>
#include <yave/framegraph/FrameGraphResourcePool.h>

#include <yave/components/DirectionalLightComponent.h>
#include <yave/components/PointLightComponent.h>
#include <yave/components/SkyLightComponent.h>
#include <yave/components/StaticMeshComponent.h>
#include <yave/components/TransformableComponent.h>
#include <yave/ecs/EntityPrefab.h>
#include <yave/ecs/EntityWorld.h>

#include <yave/entities/entities.h>
#include <yave/utils/color.h>
#include <yave/utils/entities.h>

#include <y/serde3/archives.h>

#include <y/utils/log.h>
#include <y/utils/format.h>

namespace editor {

static Texture render_texture(const AssetPtr<Texture>& tex) {
    y_profile();

    CmdBufferRecorder recorder = create_disposable_cmd_buffer(app_device());
    StorageTexture out(app_device(), ImageFormat(VK_FORMAT_R8G8B8A8_UNORM), math::Vec2ui(ThumbmailRenderer::thumbmail_size));
    {
        const DescriptorSet set(app_device(), {Descriptor(*tex, SamplerType::LinearClamp), Descriptor(StorageView(out))});
        recorder.dispatch_size(device_resources(app_device())[DeviceResources::CopyProgram],  out.size(), {set});
    }
    std::move(recorder).submit<SyncPolicy::Sync>();
    return out;
}

static Texture render_world(const ecs::EntityWorld& world) {
    y_profile();

    SceneView scene(&world);
    scene.camera().set_proj(math::perspective(math::to_rad(45.0f), 1.0f, 0.1f));
    scene.camera().set_view(math::look_at(math::Vec3(0.0f, -1.0f, 1.0f), math::Vec3(0.0f), math::Vec3(0.0f, 0.0f, 1.0f)));

    CmdBufferRecorder recorder = create_disposable_cmd_buffer(app_device());
    StorageTexture out(app_device(), ImageFormat(VK_FORMAT_R8G8B8A8_UNORM), math::Vec2ui(ThumbmailRenderer::thumbmail_size));
    {
        const auto region = recorder.region("Thumbmail cache render");

        auto resource_pool = std::make_shared<FrameGraphResourcePool>(app_device());
        FrameGraph graph(resource_pool);

        RendererSettings settings;
        settings.tone_mapping.auto_exposure = false;
        const DefaultRenderer renderer = DefaultRenderer::create(graph, scene, out.size(), settings);

        const FrameGraphImageId output_image = renderer.tone_mapping.tone_mapped;
        {
            FrameGraphPassBuilder builder = graph.add_pass("Thumbmail copy pass");
            builder.add_uniform_input(output_image);
            builder.add_uniform_input(renderer.gbuffer.depth);
            builder.add_external_input(StorageView(out));
            builder.set_render_func([size = out.size()](CmdBufferRecorder& rec, const FrameGraphPass* self) {
                    rec.dispatch_size(resources()[EditorResources::DepthAlphaProgram], size, {self->descriptor_sets()[0]});
                });
        }

        std::move(graph).render(recorder);
    }
    std::move(recorder).submit<SyncPolicy::Sync>();
    return out;
}

static void fill_world(ecs::EntityWorld& world) {
    const float intensity = 1.0f;

    {
        const ecs::EntityId light_id = world.create_entity(DirectionalLightArchetype());
        DirectionalLightComponent* light_comp = world.component<DirectionalLightComponent>(light_id);
        light_comp->direction() = math::Vec3{0.0f, 0.3f, -1.0f};
        light_comp->intensity() = 3.0f * intensity;
    }
    {
        const ecs::EntityId light_id = world.create_entity(PointLightArchetype());
        world.component<TransformableComponent>(light_id)->position() = math::Vec3(0.75f, -0.5f, 0.5f);
        PointLightComponent* light = world.component<PointLightComponent>(light_id);
        light->color() = k_to_rbg(2500.0f);
        light->intensity() = 1.5f * intensity;
        light->falloff() = 0.5f;
        light->radius() = 2.0f;
    }
    {
        const ecs::EntityId light_id = world.create_entity(PointLightArchetype());
        world.component<TransformableComponent>(light_id)->position() = math::Vec3(-0.75f, -0.5f, 0.5f);
        PointLightComponent* light = world.component<PointLightComponent>(light_id);
        light->color() = k_to_rbg(10000.0f);
        light->intensity() = 1.5f * intensity;
        light->falloff() = 0.5f;
        light->radius() = 2.0f;
    }

    {
        const ecs::EntityId sky_id = world.create_entity(ecs::StaticArchetype<SkyLightComponent>());
        world.component<SkyLightComponent>(sky_id)->probe() = device_resources(app_device()).ibl_probe();
    }

}

static math::Transform<> center_to_camera(const AABB& box) {
    const float scale = 0.22f / std::max(math::epsilon<float>, box.radius());
    const float angle = (box.extent().x() > box.extent().y() ? 90.0f : 0.0f) + 30.0f;
    const auto rot = math::Quaternion<>::from_euler(0.0f, math::to_rad(angle), 0.0f);
    const math::Vec3 tr = rot(box.center() * scale);
    return math::Transform<>(math::Vec3(0.0f, -0.65f, 0.65f) - tr,
                             rot,
                             math::Vec3(scale));
}

static Texture render_object(const AssetPtr<StaticMesh>& mesh, const AssetPtr<Material>& mat) {
    ecs::EntityWorld world;
    fill_world(world);

    {
        const ecs::EntityId entity = world.create_entity(StaticMeshArchetype());
        *world.component<StaticMeshComponent>(entity) = StaticMeshComponent(mesh, mat);
        world.component<TransformableComponent>(entity)->transform() = center_to_camera(mesh->aabb());
    }

    return render_world(world);
}

static Texture render_prefab(const AssetPtr<ecs::EntityPrefab>& prefab) {
    ecs::EntityWorld world;
    fill_world(world);

    {
        const ecs::EntityId entity = world.create_entity(*prefab);
        if(const StaticMeshComponent* mesh_comp = world.component<StaticMeshComponent>(entity)) {
            if(TransformableComponent* trans_comp = world.component<TransformableComponent>(entity)) {
                trans_comp->transform() = center_to_camera(mesh_comp->compute_aabb());
            }
        }
    }

    return render_world(world);
}


ThumbmailRenderer::ThumbmailRenderer(AssetLoader& loader) : DeviceLinked(loader.device()), _loader(&loader) {
}

const TextureView* ThumbmailRenderer::thumbmail(AssetId id) {
    y_profile();

    if(id == AssetId::invalid_id()) {
        return nullptr;
    }

    const auto lock = y_profile_unique_lock(_lock);

    auto& data = _thumbmails[id];

    if(!data) {
        data = std::make_unique<ThumbmailData>();
        query(id, *data);
        return nullptr;
    }

    if(data->failed) {
        return nullptr;
    } else if(data->view.device()) {
        return &data->view;
    } else if(data->asset_ptr.is_loaded()) {
        data->texture = data->render();
        data->render = []() -> Texture { y_fatal("Thumbmail already rendered"); };
        if(!(data->failed = data->texture.is_null())) {
            data->view = data->texture;
        }
    }

    return nullptr;
}


void ThumbmailRenderer::query(AssetId id, ThumbmailData& data) {
    const AssetType asset_type = _loader->store().asset_type(id).unwrap_or(AssetType::Unknown);

    switch(asset_type) {
        case AssetType::Mesh: {
            const auto ptr = _loader->load_async<StaticMesh>(id);
            data.asset_ptr = ptr;
            data.render = [ptr]{ return render_object(ptr, device_resources(app_device())[DeviceResources::EmptyMaterial]); };
        } break;

        case AssetType::Image: {
            const auto ptr = _loader->load_async<Texture>(id);
            data.asset_ptr = ptr;
            data.render = [ptr]{ return render_texture(ptr); };
        } break;

        case AssetType::Material: {
            const auto ptr = _loader->load_async<Material>(id);
            data.asset_ptr = ptr;
            data.render = [ptr]{ return render_object(device_resources(app_device())[DeviceResources::SphereMesh], ptr); };
        } break;

        case AssetType::Prefab: {
            const auto ptr = _loader->load_async<ecs::EntityPrefab>(id);
            data.asset_ptr = ptr;
            data.render = [ptr]{ return render_prefab(ptr); };
        } break;

        default:
            data.failed = true;
            log_msg(fmt("Unknown asset type % for %.", asset_type, id.id()), Log::Error);
        break;
    }
}

}
