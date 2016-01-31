/*******************************
Copyright (C) 2013-2015 gregoire ANGERAND

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
**********************************/
#include "TiledDeferredShadingRenderer.h"
#include "DeferredCommon.h"

namespace n {
namespace graphics {

static constexpr uint MaxLights = 1024;

Shader<ComputeShader> *TiledDeferredShadingRenderer::createComputeShader() {
	bool usePlanes = true;
	bool minMaxDepth = false;
	bool debugLightCount = false;
	return new Shader<ComputeShader>(
		"\n#define GROUP_X 32\n"
		"\n#define GROUP_Y 30\n"
		"\n#define MAX_LIGHTS " + core::String(MaxLights) + "\n"

		"layout(rgba8) uniform coherent writeonly image2D n_Out;"
		"layout(local_size_x = GROUP_X, local_size_y = GROUP_Y) in;"

		"const uint MaxUint = uint(0xFFFFFFFF);"
		"shared uint iMinDepth = MaxUint;"
		"shared uint iMaxDepth = 0;"

		+ DirectionalLightData::toShader() + ""
		+ PointLightData::toShader() + ""
		+ getBRDFs() +

		"shared uint tileLights[MAX_LIGHTS];"
		"shared uint tileLightCount = 0;"

		"uniform sampler2D n_0;"
		"uniform sampler2D n_1;"
		"uniform sampler2D n_2;"
		"uniform sampler2D n_D;"

		"uniform mat4 n_InvMatrix;"
		"uniform vec3 n_Camera;"




		"uniform n_DirectionalLightBuffer {"
			"n_DirectionalLightData n_DirectionalLights[MAX_LIGHTS];"
		"};"

		"uniform uint n_DirectionalLightCount;"


		"uniform n_PointLightBuffer {"
			"n_PointLightData n_PointLights[MAX_LIGHTS];"
		"};"

		"uniform uint n_PointLightCount;"




		"vec4 frustumPlane(vec3 cam, vec3 corner0, vec3 corner1) {"
			"vec3 norm = normalize(cross(cam - corner0, corner1 - corner0));"
			"return	vec4(-norm, dot(cam, norm));"
		"}"

		"float planeDistance(vec3 p, vec4 plane) {"
			"return dot(p, plane.xyz) + plane.w;"
		"}"

		"bool isInside(vec3 p, vec4 plane) {"
			"return planeDistance(p, plane) < 0;"
		"}"

		"bool isInside(vec3 p, float rad, vec4 plane) {"
			"return planeDistance(p, plane) < rad;"
		"}"

		"bool isInFrustum(vec3 p, vec4 planes[4]) {"
			"return isInside(p, planes[0]) &&"
				   "isInside(p, planes[1]) &&"
				   "isInside(p, planes[2]) &&"
				   "isInside(p, planes[3]);"
		"}"

		"bool isInFrustum(vec3 p, float rad, vec4 planes[4]) {"
			"return isInside(p, rad, planes[0]) &&"
				   "isInside(p, rad, planes[1]) &&"
				   "isInside(p, rad, planes[2]) &&"
				   "isInside(p, rad, planes[3]);"
		"}"

		"float rayDistance2(vec3 v, float v2, vec3 dir) {"
			"float d = dot(dir, v);"
			"return dot(v, v) - sqr(d);"
		"}"

		"bool isInFrustum(vec3 p, float rad, vec3 pos, vec3 dirs[4]) {" // not conservative
			"vec3 v = p - pos;"
			"float v2 = dot(v, v);"
			"float r2 = rad * rad;"
			"vec3 a = pos + dirs[0] * dot(dirs[0], v);"
			"vec3 b = pos + dirs[1] * dot(dirs[1], v);"
			"return dot(p - a, p - b) < 0 ||"
				   "rayDistance2(v, v2, dirs[0]) < r2 ||"
				   "rayDistance2(v, v2, dirs[1]) < r2 ||"
				   "rayDistance2(v, v2, dirs[2]) < r2 ||"
				   "rayDistance2(v, v2, dirs[3]) < r2;"

		"}"

		"vec3 unprojectNDC(vec4 vp) {"
			"vec4 p = n_InvMatrix * vp;"
			"return p.xyz / p.w;"
		"}"

		"vec3 unproject(vec2 uv, float depth) {"
			"return unprojectNDC(vec4(vec3(uv, depth) * 2.0 - vec3(1.0), 1.0));"
		"}"

		"float attenuate(float x, float rad) {"
			"return sqr(1.0 - sqr(sqr(x / rad))) / (sqr(x) + 1.0);"
		"}"

		"uint float2uint(float f) {"
			"return uint(MaxUint * f);"
		"}"

		"float uint2float(uint i) {"
			"return i / float(MaxUint);"
		"}"

		"void main() {"
			// DEPTH
			"ivec2 coord = ivec2(gl_GlobalInvocationID.xy);"
			"vec2 uv = gl_GlobalInvocationID.xy / vec2(gl_NumWorkGroups.xy * gl_WorkGroupSize.xy);"

			"float depth = texelFetch(n_D, coord, 0).x;"

			+ (minMaxDepth ?
				"uint iDepth = float2uint(depth);"
				"atomicMin(iMinDepth, iDepth);"
				"atomicMax(iMaxDepth, iDepth);"
				"float minDepth = uint2float(iMinDepth);"
				"float maxDepth = uint2float(iMaxDepth);"
				: ""
			) +

			// CULLING
			"vec3 topLeft = unprojectNDC(vec4(-1, 1, -1, 1));"
			"vec3 botLeft = unprojectNDC(vec4(-1, -1, -1, 1));"
			"vec3 botRight = unprojectNDC(vec4(1, -1, -1, 1));"

			"vec3 xStep = (botRight - botLeft) / gl_NumWorkGroups.x;"
			"vec3 yStep = (topLeft - botLeft) / gl_NumWorkGroups.y;"

			"vec3 botLeftTile = botLeft + xStep * gl_WorkGroupID.x + yStep * gl_WorkGroupID.y;"
			"vec3 botRightTile = botLeft + xStep * (gl_WorkGroupID.x + 1) + yStep * gl_WorkGroupID.y;"
			"vec3 topLeftTile = botLeft + xStep * gl_WorkGroupID.x + yStep * (gl_WorkGroupID.y + 1);"
			"vec3 topRightTile = botLeft + xStep * (gl_WorkGroupID.x + 1) + yStep * (gl_WorkGroupID.y + 1);"

			+ (usePlanes ?
				"vec4 planes[4];"
				"planes[0] = frustumPlane(n_Camera, topLeftTile, botLeftTile);" // left
				"planes[1] = frustumPlane(n_Camera, botRightTile, topRightTile);" // right
				"planes[2] = frustumPlane(n_Camera, topRightTile, topLeftTile);" // top
				"planes[3] = frustumPlane(n_Camera, botLeftTile, botRightTile);" // bottom
				:
				"vec3 dirs[4];"
				"dirs[0] = normalize(botLeftTile - n_Camera);"
				"dirs[1] = normalize(botRightTile - n_Camera);"
				"dirs[2] = normalize(topLeftTile - n_Camera);"
				"dirs[3] = normalize(topRightTile - n_Camera);"
			) +

			// G-BUFFER
			"n_GBufferData gbuffer = n_unpackGBuffer(n_0, n_1, n_2, coord);"
			"vec3 world = unproject(uv, depth);"

			"vec3 worldView = normalize(n_Camera - world);"

			"vec3 diffuseColor = mix(gbuffer.color.rgb, vec3(0.0), gbuffer.metallic);"
			"vec3 specularColor = mix(vec3(0.04), gbuffer.color.rgb, gbuffer.metallic);"


			// DIRECTIONAL SHADING
			"vec3 finalColor = vec3(0.0);"
			"for(uint i = 0; i < n_DirectionalLightCount; i++) {"
				"n_DirectionalLightData light = n_DirectionalLights[i];"

				"vec3 diffuse = brdf_lambert(light.forward, worldView, gbuffer.normal, gbuffer.roughness, diffuseColor);"
				"vec3 specular = brdf_cook_torrance(light.forward, worldView, gbuffer.normal, gbuffer.roughness, specularColor);"

				"float NoL = saturate(dot(gbuffer.normal, light.forward));"

				"finalColor += light.color * NoL * (diffuse + specular);"
			"}"


			"barrier();"

			// POINT CULLING
			"uint threadCount = gl_WorkGroupSize.x * gl_WorkGroupSize.y;"
			"uint range = 1 + (n_PointLightCount / threadCount);"
			"uint start = gl_LocalInvocationIndex * range;"
			"uint end = min(start + range, n_PointLightCount);"
			+ (minMaxDepth ? "barrier();" : "") +
			"for(uint i = start; i < end; i++) {"
				"bool intersects = isInFrustum(n_PointLights[i].position, n_PointLights[i].radius," + (usePlanes ? "planes" : "n_Camera, dirs") + ");"
				"if(intersects) {"
					"uint index = atomicAdd(tileLightCount, 1);"
					"tileLights[index] = i;"
				"}"
			"}"

			"barrier();"

			// POINT SHADING
			"if(depth != 1.0) {"
				"uint lightCount = tileLightCount;"
				"for(uint i = 0; i != lightCount; i++) {"
					"n_PointLightData light = n_PointLights[tileLights[i]];"

					"vec3 lightVec = light.position - world;"
					"float dist = length(lightVec);"
					"lightVec /= dist;"
					"dist = min(dist, light.radius);"

					"vec3 diffuse = brdf_lambert(lightVec, worldView, gbuffer.normal, gbuffer.roughness, diffuseColor);"
					"vec3 specular = brdf_cook_torrance(lightVec, worldView, gbuffer.normal, gbuffer.roughness, specularColor);"

					"float NoL = saturate(dot(gbuffer.normal, lightVec));"
					"float att = attenuate(dist / light.scale, light.radius / light.scale);"

					"finalColor += light.color * NoL * att * (diffuse + specular);"
				"}"
			"}"


			// WRITE
			+ (debugLightCount ?
				"float cc = tileLightCount;"
				"imageStore(n_Out, coord, vec4(cc / 4.0));"
				:
				"imageStore(n_Out, coord, vec4(finalColor, 1.0));"
			) +


		"}"
	);
}







TiledDeferredShadingRenderer::TiledDeferredShadingRenderer(GBufferRenderer *g, const math::Vec2ui &s) : BufferedRenderer(s, true, ImageFormat::RGBA8), gbuffer(g), compute(new ComputeShaderInstance(createComputeShader())), directionals(MaxLights), points(MaxLights) {
	compute->setValue("n_Out", getFrameBuffer().getAttachment(0), TextureAccess::WriteOnly);
	compute->setValue("n_0", gbuffer->getFrameBuffer().getAttachment(0));
	compute->setValue("n_1", gbuffer->getFrameBuffer().getAttachment(1));
	compute->setValue("n_2", gbuffer->getFrameBuffer().getAttachment(2));
	compute->setValue("n_D", gbuffer->getFrameBuffer().getDepthAttachment());
	compute->setBuffer("n_DirectionalLightBuffer", directionals);
	compute->setBuffer("n_PointLightBuffer", points);
}

TiledDeferredShadingRenderer::~TiledDeferredShadingRenderer() {
	delete compute;
}

void *TiledDeferredShadingRenderer::prepare() {
	void *ptr = gbuffer->prepare();

	SceneRenderer::FrameData *sceneData = gbuffer->getSceneRendererData(ptr);
	return new FrameData{
		ptr,
		sceneData->camera->getPosition(),
		(sceneData->proj * sceneData->view).inverse(),
		gbuffer->getRenderer()->getScene()->get<DirectionalLight>(),
		gbuffer->getRenderer()->getScene()->query<PointLight>(*sceneData->camera)
	};
}

void TiledDeferredShadingRenderer::render(void *ptr) {
	FrameData *data = reinterpret_cast<FrameData *>(ptr);
	//SceneRenderer::FrameData *sceneData = gbuffer->getSceneRendererData(data->gbufferData);

	gbuffer->render(data->gbufferData);

	for(uint i = 0; i != data->directionals.size(); i++) {
		DirectionalLight *light = data->directionals[i];
		directionals[i].color = light->getColor().sub(3) * light->getIntensity();
		directionals[i].forward = -light->getTransform().getX();
	}

	for(uint i = 0; i != data->points.size(); i++) {
		PointLight *light = data->points[i];
		points[i].color = light->getColor().sub(3) * light->getIntensity();
		points[i].radius = light->getRadius();
		points[i].position = light->getPosition();
		points[i].scale = light->getScale();
	}

	compute->setValue("n_DirectionalLightCount", data->directionals.size());
	compute->setValue("n_PointLightCount", data->points.size());

	compute->setValue("n_InvMatrix", data->inv);
	compute->setValue("n_Camera", data->camPos);
	compute->dispatch(math::Vec3ui(getFrameBuffer().getSize() / math::Vec2(32, 30), 1));

	delete data;
}

}
}
