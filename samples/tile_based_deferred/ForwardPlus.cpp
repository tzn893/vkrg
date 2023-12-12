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



#ifndef TILE_MAX_LIGHT_COUNT
#define TILE_MAX_LIGHT_COUNT 200
#endif

#ifndef MAX_TILE_COUNT
#define MAX_TILE_COUNT 8100
#endif

#ifndef MAX_LIGHT_INDEX_COUNT
#define MAX_LIGHT_INDEX_COUNT MAX_TILE_COUNT * TILE_MAX_LIGHT_COUNT
#endif

constexpr uint32_t sizeOfLightGrid = sizeof(glm::uvec2) * MAX_TILE_COUNT;
constexpr uint32_t sizeOfLightIndex = sizeof(uint32_t) * MAX_LIGHT_INDEX_COUNT;


inline Light MakeLight(int lightType, glm::vec3 vec, glm::vec3 intensity, float radius)
{
	Light light;
	glm::vec4 lvec;
	lvec.w = lightType;
	lvec.x = vec.x, lvec.y = vec.y, lvec.z = vec.z;

	light.vec = lvec;
	light.intensity = glm::vec4(intensity.x, intensity.y, intensity.z, radius);
	return light;
}


class DeferredApplication;

class TileCulling : public RenderPassInterface
{
public:
	TileCulling(RenderPass* pass, RenderPassAttachment lightGrid, RenderPassAttachment lightIndex, RenderPassAttachment lightCounter, RenderPassAttachment depth,DeferredApplication* app) :
		RenderPassInterface(pass), lightGrid(lightGrid), lightIndex(lightIndex), lightCounter(lightCounter), depth(depth), app(app) {}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;
	
	virtual RenderPassType ExpectedType()
	{
		return RenderPassType::Compute;
	}

private:

	RenderPassAttachment lightGrid;
	RenderPassAttachment lightIndex;
	RenderPassAttachment lightCounter; 
	RenderPassAttachment depth;

	DeferredApplication* app;
};

class LightCounterClear : public RenderPassInterface
{
public:
	LightCounterClear(RenderPass* pass, RenderPassAttachment lightCounter, DeferredApplication* app) :
		RenderPassInterface(pass), lightCounter(lightCounter), app(app) {}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

	virtual RenderPassType ExpectedType()
	{
		return RenderPassType::Compute;
	}

private:
	RenderPassAttachment lightCounter;
	DeferredApplication* app;
};

class PreDepthPass : public RenderPassInterface
{
public:
	PreDepthPass(RenderPass* pass, RenderPassAttachment depth, DeferredApplication* app)
	:RenderPassInterface(pass), depth(depth), app(app){}

	virtual void GetClearValue(uint32_t attachment, VkClearValue& value)
	{
		if (attachment == depth.idx)
		{
			value.depthStencil.depth = 1;
			value.depthStencil.stencil = 0;
		}
	}

	virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
		VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp) 
	{
		if (attachment == depth.idx)
		{
			loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
	}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

	virtual RenderPassType ExpectedType()
	{
		return RenderPassType::Graphics;
	}


private:
	RenderPassAttachment depth;
	DeferredApplication* app;
};


class ForwardShading : public RenderPassInterface
{
public:
	ForwardShading(RenderPass* pass, RenderPassAttachment lightIndexBuffer, RenderPassAttachment lightGrid, RenderPassAttachment depth, RenderPassAttachment color,
		DeferredApplication* app) :
		RenderPassInterface(pass), lightGrid(lightGrid), lightIndex(lightIndexBuffer), depth(depth), color(color), app(app)
	{}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

	virtual RenderPassType ExpectedType()
	{
		return RenderPassType::Graphics;
	}

	virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
		VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp)
	{
		if (attachment == depth.idx)
		{
			loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
			storeOp = VK_ATTACHMENT_STORE_OP_NONE;
			stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
		if (attachment == color.idx)
		{
			loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}
	}

private:

	RenderPassAttachment lightGrid;
	RenderPassAttachment lightIndex;
	RenderPassAttachment depth;
	RenderPassAttachment color;

	DeferredApplication* app;
};

class DeferredApplication : public Application
{
	friend class TileCulling;
	friend class LightCounterClear;
	friend class ForwardShading;
public:

	DeferredApplication(uint32_t width, uint32_t height, GVK_DEVICE_EXTENSION* exts, uint32_t extCnt, GVK_INSTANCE_EXTENSION* instances, uint32_t instCnt)
		:Application(width, height, false, exts, extCnt, instances, instCnt)
	{}

	virtual void CreateRenderGraph() override
	{
		rg = std::make_shared<RenderGraph>();

		ResourceInfo info;
		info.format = VK_FORMAT_R8G8B8A8_UNORM;

		info.format = m_BackBufferFormat;
		rg->AddGraphResource("backBuffer", info, true, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		info.format = VK_FORMAT_D24_UNORM_S8_UINT;
		rg->AddGraphResource("depthStencil", info, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		info.format = VK_FORMAT_UNDEFINED;
		info.extType = ResourceExtensionType::Buffer;
		info.ext.buffer.size = sizeOfLightGrid;
		info.usages = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		rg->AddGraphResource("lightGrid", info, false);

		// this resources  will be culled during compile stage
		rg->AddGraphResource("__useless1", info, false);
		rg->AddGraphResource("__useless2", info, false);
		rg->AddGraphResource("__useless3", info, false);
		rg->AddGraphResource("__useless4", info, false);
		rg->AddGraphResource("__useless5", info, false);
		rg->AddGraphResource("__useless6", info, false);
		rg->AddGraphResource("__useless7", info, false);
		rg->AddGraphResource("__useless8", info, false);
		rg->AddGraphResource("__useless9", info, false);
		rg->AddGraphResource("__useless10", info, false);
		rg->AddGraphResource("__useless11", info, false);
		rg->AddGraphResource("__useless12", info, false);
		rg->AddGraphResource("__useless13", info, false);
		rg->AddGraphResource("__useless14", info, false);
		rg->AddGraphResource("__useless15", info, false);
		rg->AddGraphResource("__useless16", info, false);
		rg->AddGraphResource("__useless17", info, false);
		rg->AddGraphResource("__useless18", info, false);
		rg->AddGraphResource("__useless19", info, false);
		rg->AddGraphResource("__useless20", info, false);
		rg->AddGraphResource("__useless21", info, false);
		rg->AddGraphResource("__useless22", info, false);
		rg->AddGraphResource("__useless23", info, false);
		rg->AddGraphResource("__useless24", info, false);
		rg->AddGraphResource("__useless25", info, false);
		rg->AddGraphResource("__useless26", info, false);
		rg->AddGraphResource("__useless27", info, false);
		rg->AddGraphResource("__useless28", info, false);

		info.ext.buffer.size = sizeOfLightIndex;
		rg->AddGraphResource("lightIndex", info, false);

		info.ext.buffer.size = sizeof(uint32_t);
		rg->AddGraphResource("lightCounter", info, false);

		tileCullingHandle = rg->AddGraphRenderPass("tile culling", RenderPassType::Compute).value();
		lightCounterClearHandle = rg->AddGraphRenderPass("clear light counter", RenderPassType::Compute).value();
		earlyDepthHandle = rg->AddGraphRenderPass("early depth", RenderPassType::Graphics).value();
		shadingHandle = rg->AddGraphRenderPass("shading", RenderPassType::Graphics).value();

		{
			ImageSlice slice;
			slice.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			slice.baseArrayLayer = 0;
			slice.baseMipLevel = 0;
			slice.layerCount = 1;
			slice.levelCount = 1;

			auto lightGrid = tileCullingHandle.pass->AddBufferStorageOutput("lightGrid", BufferSlice::fullBuffer).value();
			auto lightIndex = tileCullingHandle.pass->AddBufferStorageOutput("lightIndex", BufferSlice::fullBuffer).value();
			auto lightCounter = tileCullingHandle.pass->AddBufferStorageInput("lightCounter", BufferSlice::fullBuffer).value();
			auto depth = tileCullingHandle.pass->AddImageColorInput("depthStencil", slice, VK_IMAGE_VIEW_TYPE_2D).value();

			CreateRenderPassInterface<TileCulling>(tileCullingHandle.pass.get(), lightGrid, lightIndex, lightCounter, depth, this);
		}

		{
			auto lightCounter = lightCounterClearHandle.pass->AddBufferStorageOutput("lightCounter", BufferSlice::fullBuffer).value();

			CreateRenderPassInterface<LightCounterClear>(lightCounterClearHandle.pass.get(), lightCounter, this);
		}

		{
			ImageSlice slice;
			slice.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			slice.baseArrayLayer = 0;
			slice.baseMipLevel = 0;
			slice.layerCount = 1;
			slice.levelCount = 1;

			auto depthStencil = earlyDepthHandle.pass->AddImageDepthOutput("depthStencil", slice).value();
			
			CreateRenderPassInterface<PreDepthPass>(earlyDepthHandle.pass.get(), depthStencil, this);
		}

		{
			ImageSlice slice;
			slice.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			slice.baseArrayLayer = 0;
			slice.baseMipLevel = 0;
			slice.layerCount = 1;
			slice.levelCount = 1;

			auto depthStencil = shadingHandle.pass->AddImageDepthInput("depthStencil", slice).value();
			auto lightIndex = shadingHandle.pass->AddBufferStorageInput("lightIndex", BufferSlice::fullBuffer).value();
			auto lightGrid = shadingHandle.pass->AddBufferStorageInput("lightGrid", BufferSlice::fullBuffer).value();

			slice.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			auto colorOutput = shadingHandle.pass->AddImageColorOutput("backBuffer", slice).value();

			CreateRenderPassInterface<ForwardShading>(shadingHandle.pass.get(), lightIndex, lightGrid, depthStencil, colorOutput, this);
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
		lightData.count = GRID_COUNT * GRID_COUNT * 4;

		vkglTF::BoundingBox box = model.GetBox();

		float x_grid = (box.upper.x - box.lower.x) / GRID_COUNT;
		float z_grid = (box.upper.z - box.lower.z) / GRID_COUNT;
		float y_grid = (box.upper.y - box.lower.y) / GRID_COUNT * 3;

		sizeof(Light);

		glm::vec3 lower = box.lower;
		for (uint32_t y = 0; y < 4; y++)
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
					glm::vec3 pos = lower + glm::vec3(x * x_grid, 1 + y_grid * y, z * z_grid);
					lightData.lights[lightIdx] = MakeLight(LIGHT_TYPE_POINT, pos, glm::vec3(1, 1, 1) * 3.0f, 3);
				}
			}
		}

		/*
		glm::vec4 vec;
		vec.w = LIGHT_TYPE_POINT;

		lightData.count = 1;
		lightData.lights[0] = MakeLight(LIGHT_TYPE_POINT, glm::vec3(0, 1, 0), glm::vec3(10, 10, 10), 1.5);
		*/

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


		auto tileCullingComp = context->CompileShader("tile_culling.comp", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();

		auto lightCounterClearComp = context->CompileShader("tile_counter_clear.comp", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();


		auto earlyDepthVert = context->CompileShader("forward_pre_depth.vert", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();
		auto forwardShadingVert = context->CompileShader("forward_shading.vert", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();
		auto forwardShadingFrag = context->CompileShader("forward_shading.frag", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();

		auto vars = forwardShadingVert->GetInputVariables().value();
		model.loadFromFile(VKRG_MODEL_DIRECTORY"/sponza/sponza.gltf", context, vars.data(), vars.size());

		{
			GvkComputePipelineCreateInfo pipelineCI;
			pipelineCI.shader = tileCullingComp;
			tileCullingPipeline = context->CreateComputePipeline(pipelineCI).value();
		}

		{
			GvkComputePipelineCreateInfo pipelineCI;
			pipelineCI.shader = lightCounterClearComp;
			lightCounterClearPipeline = context->CreateComputePipeline(pipelineCI).value();
		}

		{
			auto [rp, idx] = rg->GetCompiledRenderPassAndSubpass(earlyDepthHandle);
			GvkGraphicsPipelineCreateInfo pipelineCI(earlyDepthVert, nullptr, rp, idx);
			pipelineCI.depth_stencil_state.enable_depth_stencil = true;
		
			earlyDepthPipeline = context->CreateGraphicsPipeline(pipelineCI).value();
		}

		{
			auto [rp, idx] = rg->GetCompiledRenderPassAndSubpass(shadingHandle);
			GvkGraphicsPipelineCreateInfo pipelineCI(forwardShadingVert, forwardShadingFrag, rp, idx);
			pipelineCI.depth_stencil_state.enable_depth_stencil = true;
			pipelineCI.depth_stencil_state.depthCompareOp = VK_COMPARE_OP_EQUAL;
			pipelineCI.depth_stencil_state.depthWriteEnable = false;

			shadingPipeline = context->CreateGraphicsPipeline(pipelineCI).value();
		}

		auto tileCullingDescriptorLayout = tileCullingPipeline->GetInternalLayout(0, (VkShaderStageFlagBits)0);
		auto lightCounterClearDescriptorLayout = lightCounterClearPipeline->GetInternalLayout(0, (VkShaderStageFlagBits)0).value();
		auto forwardPerNode = shadingPipeline->GetInternalLayout(vkglTF::perObjectBindingIndex, (VkShaderStageFlagBits)0).value();
		auto forwardPerMaterial = shadingPipeline->GetInternalLayout(vkglTF::perMaterialBindingIndex, (VkShaderStageFlagBits)0).value();
		auto forwardPerDraw = shadingPipeline->GetInternalLayout(vkglTF::perDrawBindingIndex, (VkShaderStageFlagBits)0).value();
		auto forwardPerCamera = shadingPipeline->GetInternalLayout(vkglTF::perCameraBindingIndex, (VkShaderStageFlagBits)0).value();

		auto preDepthPerNode = earlyDepthPipeline->GetInternalLayout(vkglTF::perObjectBindingIndex, (VkShaderStageFlagBits)0).value();
		auto preDepthPerCamera = earlyDepthPipeline->GetInternalLayout(vkglTF::perCameraBindingIndex, (VkShaderStageFlagBits)0).value();

		model.createDescriptorsForPipeline(descriptorAlloc, shadingPipeline);

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
		defaultSampler = context->CreateSampler(samplerCI).value();

		tileCullingDescriptor = descriptorAlloc->Allocate(tileCullingDescriptorLayout.value()).value();
		lightCounterClearDescriptor = descriptorAlloc->Allocate(lightCounterClearDescriptorLayout).value();
		
		forwardPerDrawDescriptor = descriptorAlloc->Allocate(forwardPerDraw).value();
		forwardPerCameraDescriptor = descriptorAlloc->Allocate(forwardPerCamera).value();

		preDepthPerNodeDescriptor = descriptorAlloc->Allocate(preDepthPerNode).value();
		preDepthPerCameraDescriptor = descriptorAlloc->Allocate(preDepthPerCamera).value();

		GvkDescriptorSetWrite()
			.BufferWrite(tileCullingDescriptor, "lightUBO", lightBuffer->GetBuffer(), 0, lightBuffer->GetSize())
			.BufferWrite(tileCullingDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, cameraBuffer->GetSize())
			.BufferWrite(tileCullingDescriptor, "screenUBO", screenBuffer->GetBuffer(), 0, screenBuffer->GetSize())
			.BufferWrite(forwardPerCameraDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, cameraBuffer->GetSize())
			.BufferWrite(forwardPerDrawDescriptor, "lightUBO", lightBuffer->GetBuffer(), 0, lightBuffer->GetSize())
			.BufferWrite(forwardPerDrawDescriptor, "screenUBO", screenBuffer->GetBuffer(), 0 , screenBuffer->GetSize())
			.BufferWrite(preDepthPerCameraDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, cameraBuffer->GetSize())
			.Emit(context->GetDevice());

		return true;
	}

	void MainLoop(VkCommandBuffer cmd, uint32_t imageIdx) override
	{

		CameraUBO cameraData = m_Camera.GetCameraUBO();
		cameraBuffer->Write(&cameraData, 0, sizeof(CameraUBO));
		rg->Execute(imageIdx, cmd);
	}

	RenderPassHandle tileCullingHandle;
	RenderPassHandle lightCounterClearHandle;
	RenderPassHandle earlyDepthHandle;
	RenderPassHandle shadingHandle;

	gvk::ptr<gvk::Pipeline>	tileCullingPipeline;
	gvk::ptr<gvk::Pipeline> lightCounterClearPipeline;
	gvk::ptr<gvk::Pipeline> shadingPipeline;
	gvk::ptr<gvk::Pipeline> earlyDepthPipeline;

	gvk::ptr<gvk::Buffer>			lightBuffer;
	Lights							lightData;
	gvk::ptr<gvk::Buffer>			cameraBuffer;
	gvk::ptr<gvk::Buffer>			screenBuffer;
	Screen							screenData;
	VkSampler						defaultSampler;

	gvk::ptr<gvk::DescriptorSet>	tileCullingDescriptor;
	gvk::ptr<gvk::DescriptorSet>	lightCounterClearDescriptor;

	gvk::ptr<gvk::DescriptorSet>	forwardPerDrawDescriptor;
	gvk::ptr<gvk::DescriptorSet>	forwardPerCameraDescriptor;

	gvk::ptr<gvk::DescriptorSet>	preDepthPerNodeDescriptor;
	gvk::ptr<gvk::DescriptorSet>	preDepthPerCameraDescriptor;

	vkglTF::Model model;
};



int main()
{

	GVK_INSTANCE_EXTENSION extraInstances[] = {GVK_INSTANCE_EXTENSION_SHADER_PRINT};

	constexpr uint32_t initWidth = 800, initHeight = 800;
	std::make_shared<DeferredApplication>(initWidth, initHeight, (GVK_DEVICE_EXTENSION*)NULL, 0, extraInstances, gvk_count_of(extraInstances))->Run();

	return 0;
}

void TileCulling::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	if (ctx.CheckAttachmentDirtyFlag(lightIndex) || ctx.CheckAttachmentDirtyFlag(lightCounter) || ctx.CheckAttachmentDirtyFlag(lightGrid) || ctx.GetImageAttachment(depth))
	{
		BufferView lightIndexBuffer = ctx.GetBufferAttachment(lightIndex);
		BufferView lightCounterBuffer = ctx.GetBufferAttachment(lightCounter);
		BufferView lightGridBuffer = ctx.GetBufferAttachment(lightGrid);

		VkImageView depthStencil = ctx.GetImageAttachment(depth);

		GvkDescriptorSetWrite()
			.BufferWrite(app->tileCullingDescriptor, "lightIndex", lightIndexBuffer.buffer, lightIndexBuffer.offset, lightIndexBuffer.size)
			.BufferWrite(app->tileCullingDescriptor, "lightCounter", lightCounterBuffer.buffer, lightCounterBuffer.offset, lightCounterBuffer.size)
			.BufferWrite(app->tileCullingDescriptor, "lightGrid", lightGridBuffer.buffer, lightGridBuffer.offset, lightGridBuffer.size)
			.ImageWrite(app->tileCullingDescriptor, "depthSampler", app->defaultSampler, depthStencil, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		.Emit(app->context->GetDevice());
	}

	GvkBindPipeline(cmd, app->tileCullingPipeline);
	
	GvkDescriptorSetBindingUpdate(cmd, app->tileCullingPipeline)
	.BindDescriptorSet(app->tileCullingDescriptor)
	.Update();

	vkCmdDispatch(cmd, (uint32_t)app->screenData.tileCount.x, (uint32_t)app->screenData.tileCount.y, 1);
}

void LightCounterClear::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	if (ctx.CheckAttachmentDirtyFlag(lightCounter))
	{
		BufferView lightCounterBuffer = ctx.GetBufferAttachment(lightCounter);

		GvkDescriptorSetWrite()
			.BufferWrite(app->lightCounterClearDescriptor, "lightCounter", lightCounterBuffer.buffer, lightCounterBuffer.offset, lightCounterBuffer.size)
		.Emit(app->context->GetDevice());
	}

	GvkBindPipeline(cmd, app->lightCounterClearPipeline);

	GvkDescriptorSetBindingUpdate(cmd, app->lightCounterClearPipeline)
	.BindDescriptorSet(app->lightCounterClearDescriptor)
	.Update();

	vkCmdDispatch(cmd , 1, 1, 1);
}

void PreDepthPass::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	GvkBindPipeline(cmd, app->earlyDepthPipeline);

	GvkDescriptorSetBindingUpdate(cmd, app->earlyDepthPipeline)
		.BindDescriptorSet(app->preDepthPerCameraDescriptor)
	.Update();

	app->model.draw(cmd, vkglTF::RenderOpaqueNodes);
}

void ForwardShading::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	if (ctx.CheckAttachmentDirtyFlag(lightIndex) || ctx.CheckAttachmentDirtyFlag(lightGrid))
	{
		auto lightIndexBuffer = ctx.GetBufferAttachment(lightIndex);
		auto lightGridBuffer = ctx.GetBufferAttachment(lightGrid);

		GvkDescriptorSetWrite()
			.BufferWrite(app->forwardPerDrawDescriptor, "lightIndex", lightIndexBuffer.buffer, lightIndexBuffer.offset, lightIndexBuffer.size)
			.BufferWrite(app->forwardPerDrawDescriptor, "lightGrid", lightGridBuffer.buffer, lightGridBuffer.offset, lightGridBuffer.size)
		.Emit(app->context->GetDevice());
	}

	GvkBindPipeline(cmd, app->shadingPipeline);

	GvkDescriptorSetBindingUpdate(cmd, app->shadingPipeline)
		.BindDescriptorSet(app->forwardPerCameraDescriptor)
		.BindDescriptorSet(app->forwardPerDrawDescriptor)
	.Update();

	app->model.draw(cmd, vkglTF::BindImages | vkglTF::RenderOpaqueNodes);
}
