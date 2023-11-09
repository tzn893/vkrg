#include "Application.h"
#include "DeferredPass.h"
#include "DeferredShading.h"
#include <vkrg/graph.h>
#include "Model.h"
using namespace vkrg;

constexpr int GRID_HEIGHT = 8;
constexpr int GRID_COUNT = 16;
constexpr int MAX_LIGHT_COUNT = GRID_COUNT * GRID_COUNT * GRID_HEIGHT - 1;

#define TILE_SIZE 16


#ifndef LIGHT_TYPE_POINT
#define LIGHT_TYPE_POINT 0
#endif

#ifndef LIGHT_TYPE_DIRECT
#define LIGHT_TYPE_DIRECT 1
#endif

struct Light
{
	glm::vec4 intensity;
	glm::vec4 vec;
};

struct Lights
{
	Light lights[MAX_LIGHT_COUNT];
	int count;
};

struct Screen
{
	// (resolution_x, resolution_y, 1 / resolution_x, 1 / resolution_y)
	glm::vec4  resolution;
	// (tile_count_x, tile_count_y, 1 / tile_count_x, 1 / tile_count_y)
	glm::vec4  tileCount;
};

struct TileLighting
{
	ivec4 lightCount;
	vec4  padding;
	Light lights[15];
};

struct CullingResult
{
	TileLighting tileLighting[8100];
};



inline Light MakeLight(int lightType, glm::vec3 vec, glm::vec3 intensity)
{
	Light light;
	glm::vec4 lvec;
	lvec.w = lightType;
	lvec.x = vec.x, lvec.y = vec.y, lvec.z = vec.z;

	light.vec = lvec;
	light.intensity = glm::vec4(intensity.x, intensity.y, intensity.z, 1);
	return light;
}

class DeferredApplication;

class MyDeferredPass : public DeferredPass
{
public:
	MyDeferredPass(RenderPass* targetPass, RenderPassAttachment normal, RenderPassAttachment color,
		RenderPassAttachment material, RenderPassAttachment depthStencil, DeferredApplication* app)
		:DeferredPass(targetPass, normal, color, material, depthStencil), app(app)
	{

	}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

private:
	DeferredApplication* app;
};

class MyDeferredShading : public  DeferredShading
{
public:
	MyDeferredShading(RenderPass* targetPass, RenderPassAttachment normal, RenderPassAttachment color,
		RenderPassAttachment material, RenderPassAttachment color_output, RenderPassAttachment depth,
		RenderPassAttachment cullingResult, DeferredApplication* app)
		:DeferredShading(targetPass, normal, color, material, color_output, depth), 
		app(app), cullingResult(cullingResult)
	{}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

	virtual RenderPassType ExpectedType() override { return RenderPassType::Graphics; }

	virtual bool OnValidationCheck(std::string& msg) override;

private:

	RenderPassAttachment cullingResult;
	DeferredApplication* app;
};


class TileCulling : public RenderPassInterface
{
public:
	TileCulling(RenderPass* pass, RenderPassAttachment cullingResult, DeferredApplication* app) :
		RenderPassInterface(pass), cullingResult(cullingResult), app(app) {}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;
	
	virtual RenderPassType ExpectedType()
	{
		return RenderPassType::Compute;
	}

private:

	RenderPassAttachment cullingResult;
	DeferredApplication* app;
};

class DeferredApplication : public Application
{
	friend class MyDeferredPass;
	friend class MyDeferredShading;
	friend class TileCulling;
public:

	DeferredApplication(uint32_t width, uint32_t height)
		:Application(width, height, false)
	{}

	virtual void CreateRenderGraph() override
	{
		rg = std::make_shared<RenderGraph>();

		ResourceInfo info;
		info.format = VK_FORMAT_R8G8B8A8_UNORM;

		rg->AddGraphResource("color", info, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		rg->AddGraphResource("material", info, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		rg->AddGraphResource("normal", info, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		info.format = m_BackBufferFormat;
		rg->AddGraphResource("backBuffer", info, true, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		info.format = VK_FORMAT_D24_UNORM_S8_UINT;
		rg->AddGraphResource("depthStencil", info, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		info.format = VK_FORMAT_UNDEFINED;
		info.extType = ResourceExtensionType::Buffer;
		info.ext.buffer.size = sizeof(CullingResult);
		info.usages = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		rg->AddGraphResource("cullingResult", info, false);

		deferredPassHandle = rg->AddGraphRenderPass("deferred pass", RenderPassType::Graphics).value();
		deferredShadingHandle = rg->AddGraphRenderPass("deferred shading", RenderPassType::Graphics).value();
		tileCullingHandle = rg->AddGraphRenderPass("tile culling", RenderPassType::Compute).value();

		{
			BufferSlice slice;
			slice.offset = 0;
			slice.size = sizeof(CullingResult);

			auto cullingResult = tileCullingHandle.pass->AddBufferStorageOutput("cullingResult", slice).value();

			CreateRenderPassInterface<TileCulling>(tileCullingHandle.pass.get(), cullingResult, this);
		}

		{
			ImageSlice range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			range.baseMipLevel = 0;
			range.levelCount = 1;

			auto pass = deferredPassHandle.pass;
			auto color = pass->AddImageColorOutput("color", range).value();
			auto material = pass->AddImageColorOutput("material", range).value();
			auto normal = pass->AddImageColorOutput("normal", range).value();

			range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			auto depthStencil = pass->AddImageDepthOutput("depthStencil", range, VK_IMAGE_VIEW_TYPE_2D).value();

			CreateRenderPassInterface<MyDeferredPass>(pass.get(), normal, color, material, depthStencil, this);
		}

		{
			ImageSlice range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			range.baseMipLevel = 0;
			range.levelCount = 1;

			auto pass = deferredShadingHandle.pass;

			auto color = pass->AddImageColorInput("color", range, VK_IMAGE_VIEW_TYPE_2D).value();
			auto material = pass->AddImageColorInput("material", range, VK_IMAGE_VIEW_TYPE_2D).value();
			auto normal = pass->AddImageColorInput("normal", range, VK_IMAGE_VIEW_TYPE_2D).value();

			auto backBuffer = pass->AddImageColorOutput("backBuffer", range, VK_IMAGE_VIEW_TYPE_2D).value();

			range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			auto depth = pass->AddImageColorInput("depthStencil", range, VK_IMAGE_VIEW_TYPE_2D).value();

			BufferSlice slice;
			slice.offset = 0;
			slice.size = sizeof(CullingResult);

			auto cullingResult = pass->AddBufferStorageInput("cullingResult", slice).value();
			CreateRenderPassInterface<MyDeferredShading>(pass.get(), normal, color, material, backBuffer, depth, cullingResult, this);
		}

		RenderGraphDeviceContext deviceCtx;
		deviceCtx.ctx = context;

		RenderGraphCompileOptions options;
		options.addAutomaticTransferUsageFlag = true;
		options.flightFrameCount = backBufferCount;
		options.screenWidth = windowWidth;
		options.screenHeight = windowHeight;
		options.style = RenderGraphRenderPassStyle::MergeGraphicsPasses;
		options.disableFrameOnFlight = true;

		auto [state, msg] = rg->Compile(options, deviceCtx);

		auto backBuffers = context->GetBackBuffers();
		auto dataFrame = rg->GetExternalDataFrame();

		for (uint32_t i = 0; i < backBuffers.size(); i++)
		{
			dataFrame.BindImage("backBuffer", i, backBuffers[i]);
		}

	}


	void IntializeLighting()
	{
		glm::vec4 vec;
		vec.w = LIGHT_TYPE_POINT;
		lightData.count = MAX_LIGHT_COUNT;

		vkglTF::BoundingBox box = model.GetBox();

		float x_grid = (box.upper.x - box.lower.x) / GRID_COUNT;
		float z_grid = (box.upper.z - box.lower.z) / GRID_COUNT;
		float y_grid = (box.upper.y - box.upper.y) / GRID_COUNT;

		sizeof(Light);

		glm::vec3 lower = box.lower;
		for (uint32_t y = 0; y < GRID_HEIGHT; y++)
		{
			for (uint32_t z = 0; z < GRID_COUNT; z++)
			{
				for (uint32_t x = 0; x < GRID_COUNT; x++)
				{
					uint32_t lightIdx = x + z * GRID_COUNT + y * GRID_COUNT * GRID_COUNT;

					if (lightIdx >= MAX_LIGHT_COUNT)
					{
						break;
					}
					glm::vec3 pos = lower + glm::vec3(x * x_grid, y * y_grid, z * z_grid);
					lightData.lights[lightIdx] = MakeLight(LIGHT_TYPE_POINT, pos + glm::vec3(0, 1, 0), glm::vec3(0.1, 0.1, 0.1));
				}
			}
		}

	}

	virtual bool CustomInitialize() override
	{
		screenData.resolution.x = windowWidth;
		screenData.resolution.y = windowHeight;
		screenData.resolution.z = 1.0f / (float)windowHeight;
		screenData.resolution.w = 1.0f / (float)windowWidth;

		screenData.tileCount.x = (windowWidth + TILE_SIZE - 1) / TILE_SIZE;
		screenData.tileCount.y = (windowHeight + TILE_SIZE - 1) / TILE_SIZE;
		screenData.tileCount.z = 1 / screenData.tileCount.x;
		screenData.tileCount.w = 1 / screenData.tileCount.y;

		screenBuffer = context->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(Screen), GVK_HOST_WRITE_SEQUENTIAL).value();
		screenBuffer->Write(&screenData, 0, sizeof(screenData));

		std::string error;
		const char* include_directorys[] = { VKRG_SHADER_DIRECTORY };

		auto deferredShadingVert = context->CompileShader("deferred.vert", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();
		auto deferredShadingFrag = context->CompileShader("tile_deferred.frag", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();
		auto deferredPassVert = context->CompileShader("mrt.vert", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();
		auto deferredPassFrag = context->CompileShader("mrt.frag", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();

		auto tileCullingComp = context->CompileShader("tile_culling.comp", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();


		auto vars = deferredPassVert->GetInputVariables().value();
		model.loadFromFile(VKRG_MODEL_DIRECTORY"/sponza/sponza.gltf", context, vars.data(), vars.size());

		{
			auto [rp, idx] = rg->GetCompiledRenderPassAndSubpass(deferredShadingHandle);
			GvkGraphicsPipelineCreateInfo pipelineCI(deferredShadingVert, deferredShadingFrag, rp, idx);
			pipelineCI.rasterization_state.cullMode = VK_CULL_MODE_NONE;
			deferredShadingPipeline = context->CreateGraphicsPipeline(pipelineCI).value();
		}

		{
			auto [rp, idx] = rg->GetCompiledRenderPassAndSubpass(deferredPassHandle);
			GvkGraphicsPipelineCreateInfo pipelineCI(deferredPassVert, deferredPassFrag, rp, idx);
			pipelineCI.depth_stencil_state.enable_depth_stencil = true;
			deferredPassPipeline = context->CreateGraphicsPipeline(pipelineCI).value();
		}

		{
			GvkComputePipelineCreateInfo pipelineCI;
			pipelineCI.shader = tileCullingComp;
			tileCullingPipeline = context->CreateComputePipeline(pipelineCI).value();
		}

		auto cameraLayout = deferredPassPipeline->GetInternalLayout(vkglTF::perCameraBindingIndex, (VkShaderStageFlagBits)0);
		auto shadingPerdrawLayout = deferredShadingPipeline->GetInternalLayout(vkglTF::perDrawBindingIndex, (VkShaderStageFlagBits)0);
		auto materialLayout = deferredShadingPipeline->GetInternalLayout(vkglTF::perMaterialBindingIndex, (VkShaderStageFlagBits)0);

		auto shadingCameraLayout = deferredShadingPipeline->GetInternalLayout(vkglTF::perCameraBindingIndex, (VkShaderStageFlagBits)0);
		auto tileCullingDescriptorLayout = tileCullingPipeline->GetInternalLayout(0, (VkShaderStageFlagBits)0);

		model.createDescriptorsForPipeline(descriptorAlloc, deferredPassPipeline);

		//lightData.count = 0;
		IntializeLighting();
		
		lightBuffer = context->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(lightData), GVK_HOST_WRITE_SEQUENTIAL).value();
		lightBuffer->Write(&lightData, 0, sizeof(lightData));

		cameraBuffer = context->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(CameraUBO), GVK_HOST_WRITE_SEQUENTIAL).value();

		VkSamplerCreateInfo samplerCI{};
		samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCI.magFilter = VK_FILTER_LINEAR;
		samplerCI.minFilter = VK_FILTER_LINEAR;
		samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		samplerCI.compareOp = VK_COMPARE_OP_NEVER;
		samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		samplerCI.maxAnisotropy = 1.0;
		samplerCI.anisotropyEnable = VK_FALSE;
		samplerCI.maxLod = (float)1;
		backBufferSampler = context->CreateSampler(samplerCI).value();

		shadingPerDrawDescriptor = descriptorAlloc->Allocate(shadingPerdrawLayout.value()).value();
		cameraDescriptor = descriptorAlloc->Allocate(cameraLayout.value()).value();
		materialDescriptor = descriptorAlloc->Allocate(materialLayout.value()).value();
		shadingCameraDescriptor = descriptorAlloc->Allocate(shadingCameraLayout.value()).value();
		tileCullingDescriptor = descriptorAlloc->Allocate(tileCullingDescriptorLayout.value()).value();


		GvkDescriptorSetWrite()
			.BufferWrite(shadingPerDrawDescriptor, "screenUBO", screenBuffer->GetBuffer(), 0, screenBuffer->GetSize())
			.BufferWrite(cameraDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, cameraBuffer->GetSize())
			.BufferWrite(shadingCameraDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, cameraBuffer->GetSize())
			.BufferWrite(tileCullingDescriptor, "lightUBO", lightBuffer->GetBuffer(), 0, lightBuffer->GetSize())
			.BufferWrite(tileCullingDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, cameraBuffer->GetSize())
			.BufferWrite(tileCullingDescriptor, "screenUBO", screenBuffer->GetBuffer(), 0, screenBuffer->GetSize())
			.Emit(context->GetDevice());

		return true;
	}

	void MainLoop(VkCommandBuffer cmd, uint32_t imageIdx) override
	{
		rg->Execute(imageIdx, cmd);
	}

	RenderPassHandle deferredShadingHandle;
	RenderPassHandle deferredPassHandle;
	RenderPassHandle tileCullingHandle;

	gvk::ptr<gvk::Pipeline> deferredShadingPipeline;
	gvk::ptr<gvk::Pipeline> deferredPassPipeline;
	gvk::ptr<gvk::Pipeline>	tileCullingPipeline;

	gvk::ptr<gvk::Buffer>			lightBuffer;
	Lights							lightData;
	gvk::ptr<gvk::DescriptorSet>	shadingPerDrawDescriptor;

	gvk::ptr<gvk::Buffer>			cameraBuffer;
	gvk::ptr<gvk::DescriptorSet>    cameraDescriptor;
	gvk::ptr<gvk::DescriptorSet>	shadingCameraDescriptor;

	gvk::ptr<gvk::Buffer>			screenBuffer;
	Screen							screenData;

	gvk::ptr<gvk::DescriptorSet>	materialDescriptor;
	VkSampler						backBufferSampler;

	gvk::ptr<gvk::DescriptorSet>	tileCullingDescriptor;

	vkglTF::Model model;
};

void MyDeferredPass::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	CameraUBO cameraData = app->m_Camera.GetCameraUBO();
	app->cameraBuffer->Write(&cameraData, 0, sizeof(CameraUBO));

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->deferredPassPipeline->GetPipeline());

	GvkDescriptorSetBindingUpdate(cmd, app->deferredPassPipeline)
		.BindDescriptorSet(app->cameraDescriptor)
		.Update();

	app->model.draw(cmd, vkglTF::BindImages | vkglTF::RenderOpaqueNodes);
}

void MyDeferredShading::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{


	if (ctx.CheckAttachmentDirtyFlag(normal) || ctx.CheckAttachmentDirtyFlag(material) || ctx.CheckAttachmentDirtyFlag(color))
	{
		VkImageView normalView = ctx.GetImageAttachment(normal);
		VkImageView materialView = ctx.GetImageAttachment(material);
		VkImageView colorView = ctx.GetImageAttachment(color);
		VkImageView depthView = ctx.GetImageAttachment(depth);

		GvkDescriptorSetWrite()
			.ImageWrite(app->materialDescriptor, "depthSampler", app->backBufferSampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.ImageWrite(app->materialDescriptor, "normalSampler", app->backBufferSampler, normalView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.ImageWrite(app->materialDescriptor, "materialSampler", app->backBufferSampler, materialView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.ImageWrite(app->materialDescriptor, "colorSampler", app->backBufferSampler, colorView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.Emit(app->context->GetDevice());
	}

	if (ctx.CheckAttachmentDirtyFlag(cullingResult))
	{
		BufferView cullingResultBuffer = ctx.GetBufferAttachment(cullingResult);
		GvkDescriptorSetWrite()
		.BufferWrite(app->shadingPerDrawDescriptor, "cullingResult", 
			cullingResultBuffer.buffer, 
			cullingResultBuffer.offset, 
			cullingResultBuffer.size)
		.Emit(app->context->GetDevice());
	}


	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->deferredShadingPipeline->GetPipeline());

	GvkDescriptorSetBindingUpdate(cmd, app->deferredShadingPipeline)
		.BindDescriptorSet(app->shadingCameraDescriptor)
		.BindDescriptorSet(app->shadingPerDrawDescriptor)
		.BindDescriptorSet(app->materialDescriptor)
		.Update();

	vkCmdDraw(cmd, 6, 1, 0, 0);
}

bool MyDeferredShading::OnValidationCheck(std::string& msg)
{
	if (!DeferredShading::OnValidationCheck(msg))
	{
		return false;
	}



	return true;
}



int main()
{
	constexpr uint32_t initWidth = 800, initHeight = 800;
	std::make_shared<DeferredApplication>(initWidth, initHeight)->Run();

	return 0;
}

void TileCulling::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	if (ctx.CheckAttachmentDirtyFlag(cullingResult))
	{
		BufferView cullingResultBuffer = ctx.GetBufferAttachment(cullingResult);

		GvkDescriptorSetWrite()
		.BufferWrite(app->tileCullingDescriptor, "cullingResult", cullingResultBuffer.buffer, cullingResultBuffer.offset, cullingResultBuffer.size)
		.Emit(app->context->GetDevice());
	}

	GvkBindPipeline(cmd, app->tileCullingPipeline);
	
	GvkDescriptorSetBindingUpdate(cmd, app->tileCullingPipeline)
	.BindDescriptorSet(app->tileCullingDescriptor)
	.Update();

	vkCmdDispatch(cmd, (uint32_t)app->screenData.tileCount.x, (uint32_t)app->screenData.tileCount.y, 1);
}
