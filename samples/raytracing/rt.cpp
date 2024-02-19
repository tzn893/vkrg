#include "Application.h"
#include "DeferredPass.h"
#include "DeferredShading.h"
#include <vkrg/graph.h>
#include "Model.h"
using namespace vkrg;

constexpr int MAX_LIGHT_COUNT = 16;

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

struct GlobalUniforms
{
	glm::mat4 projInverse;
	glm::mat4 viewInverse;
	uint32_t randomSeed;
	float aoTraceDistance;
	uint32_t disableAccumulation;
};


struct AccumulateUniform
{
	int baseColor;
	int applyAO;
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

class RTAOApplication;

class MyDeferredPass : public DeferredPass
{
public:
	MyDeferredPass(RenderPass* targetPass, RenderPassAttachment normal, RenderPassAttachment color,
		RenderPassAttachment material, RenderPassAttachment depthStencil, RTAOApplication* app)
		:DeferredPass(targetPass, normal, color, material, depthStencil), app(app)
	{

	}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

private:
	RTAOApplication* app;
};

/*
class MyDeferredShading : public  DeferredShading
{
public:
	MyDeferredShading(RenderPass* targetPass, RenderPassAttachment normal, RenderPassAttachment color,
		RenderPassAttachment material, RenderPassAttachment color_output, RenderPassAttachment depth, RTAOApplication* app)
		:DeferredShading(targetPass, normal, color, material, color_output, depth), app(app)
	{

	}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

private:

	RTAOApplication* app;
};
*/

class RTPass : public RenderPassInterface
{
public:
	RTPass(RenderPass* targetPass, RenderPassAttachment normal,
		RenderPassAttachment rtTex, RenderPassAttachment depth, RTAOApplication* app)
		:
		RenderPassInterface(targetPass), normal(normal),rtTex(rtTex), depth(depth),app(app)
	{

	}

	// this function should not be called
	virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
		VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp) override
	{
		vkrg_assert(false);
	}

	virtual RenderPassType ExpectedType() override { return RenderPassType::Raytracing; }

	virtual bool OnValidationCheck(std::string& msg) override {return true;}

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

protected:

	RenderPassAttachment normal;
	RenderPassAttachment rtTex;
	RenderPassAttachment depth;
    RTAOApplication* app;
};

class AccumulatePass : public RenderPassInterface
{
public:
	AccumulatePass(RenderPass* targetPass, RenderPassAttachment rtTex,RenderPassAttachment colorOutput, RenderPassAttachment gColor, RTAOApplication* app)
		:RenderPassInterface(targetPass), rtTex(rtTex), colorOutput(colorOutput), gColor(gColor), app(app)
	{

	}

	virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
		VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp) override
	{
		vkrg_assert(false);
	}

	virtual RenderPassType ExpectedType() override { return RenderPassType::Compute; }

	virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) override;

private:
	RenderPassAttachment rtTex;
	RenderPassAttachment colorOutput;
	RenderPassAttachment gColor;
	RTAOApplication* app;
};

GVK_DEVICE_EXTENSION exts[] = { GVK_DEVICE_EXTENSION_RAYTRACING , GVK_DEVICE_EXTENSION_DEBUG_MARKER};

class RTAOApplication : public Application
{
	friend class MyDeferredPass;
	//friend class MyDeferredShading;
	friend class RTPass;
	friend class AccumulatePass;

public:

	RTAOApplication(uint32_t width, uint32_t height)
		:Application(width, height, false, exts, gvk_count_of(exts))
	{}

	virtual void CreateRenderGraph() override
	{
		rg = std::make_shared<RenderGraph>();

		ResourceInfo info;
		info.format = VK_FORMAT_R8G8B8A8_UNORM;

		rg->AddGraphResource("color", info, false);
		rg->AddGraphResource("material", info, false);
		rg->AddGraphResource("normal", info, false);

		info.format = VK_FORMAT_R32G32_SFLOAT;
		rg->AddGraphResource("rtAO", info, false);

		info.format = m_BackBufferFormat;
		rg->AddGraphResource("backBuffer", info, true, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

		info.format = VK_FORMAT_D24_UNORM_S8_UINT;
		rg->AddGraphResource("depthStencil", info, false);

		deferredPassHandle = rg->AddGraphRenderPass("deferredPass", RenderPassType::Graphics).value();
		rtAOHandle = rg->AddGraphRenderPass("rtAo", RenderPassType::Raytracing).value();
		accumulateHandle = rg->AddGraphRenderPass("accumulate", RenderPassType::Compute).value();
		 
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

		/*
		{
			ImageSlice range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			range.baseMipLevel = 0;
			range.levelCount = 1;

			auto pass = deferredShadingHandle.pass;

			auto& color = pass->AddImageColorInput("color", range, VK_IMAGE_VIEW_TYPE_2D).value();
			auto& material = pass->AddImageColorInput("material", range, VK_IMAGE_VIEW_TYPE_2D).value();
			auto& normal = pass->AddImageColorInput("normal", range, VK_IMAGE_VIEW_TYPE_2D).value();

			auto& backBuffer = pass->AddImageColorOutput("backBuffer", range, VK_IMAGE_VIEW_TYPE_2D).value();

			range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			auto& depth = pass->AddImageColorInput("depthStencil", range, VK_IMAGE_VIEW_TYPE_2D).value();

			CreateRenderPassInterface<MyDeferredShading>(pass.get(), normal, color, material, backBuffer, depth, this);
		}

		*/
		{
			ImageSlice range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			range.baseMipLevel = 0;
			range.levelCount = 1;


			auto pass = rtAOHandle.pass;
			auto rtTex = pass->AddImageRTOutput("rtAO", range, VK_IMAGE_VIEW_TYPE_2D);
			auto normal = pass->AddImageRTInput("normal", range, VK_IMAGE_VIEW_TYPE_2D);

			range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			auto depth = pass->AddImageRTSampledInput("depthStencil", range, VK_IMAGE_VIEW_TYPE_2D);


			CreateRenderPassInterface<RTPass>(pass.get(), normal.value(), rtTex.value(), depth.value(), this);
		}

		{
			ImageSlice range;
			range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			range.baseArrayLayer = 0;
			range.layerCount = 1;
			range.baseMipLevel = 0;
			range.levelCount = 1;

			auto pass = accumulateHandle.pass;
			auto rtTex = pass->AddImageStorageInput("rtAO", range, VK_IMAGE_VIEW_TYPE_2D);
			auto color = pass->AddImageStorageOutput("backBuffer", range, VK_IMAGE_VIEW_TYPE_2D);
			auto gColor = pass->AddImageStorageInput("color", range, VK_IMAGE_VIEW_TYPE_2D);

			CreateRenderPassInterface<AccumulatePass>(pass.get(), rtTex.value(), color.value(), gColor.value(), this);
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
		options.setDebugName = true;

		auto [state, msg] = rg->Compile(options, deviceCtx);

		auto& backBuffers = context->GetBackBuffers();
		auto& dataFrame = rg->GetExternalDataFrame();

		for (uint32_t i = 0; i < backBuffers.size(); i++)
		{
			dataFrame.BindImage("backBuffer", i, backBuffers[i]);
			backBuffers[i]->SetDebugName("Back_Buffer_" + std::to_string(i));
		}

	}


	void IntializeLighting()
	{
		glm::vec4 vec;
		vec.w = LIGHT_TYPE_POINT;

		lightData.count = 1;
		lightData.lights[0] = MakeLight(LIGHT_TYPE_POINT, glm::vec3(0, 1, 0), glm::vec3(100, 100, 100));
	}

	virtual void CameraMoveEvent() override
	{
		aoUniformBuffer.disableAccumulation = 1;
		aoUniformBuffer.randomSeed = 0;
	}

	virtual bool CustomInitialize() override
	{

		std::string error;
		const char* include_directorys[] = { VKRG_SHADER_DIRECTORY };
		const char* rt_include_directorys[] = {RT_SHADER_DIRECTORY};

		
		auto deferredPassVert = context->CompileShader("mrt.vert", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();
		auto deferredPassFrag = context->CompileShader("mrt.frag", gvk::ShaderMacros(),
			include_directorys, gvk_count_of(include_directorys),
			include_directorys, gvk_count_of(include_directorys),
			&error).value();
		auto rtRayGen = context->CompileShader("ao.rgen", gvk::ShaderMacros(),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			&error);
		auto rtMiss = context->CompileShader("ao.rmiss", gvk::ShaderMacros(),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			&error);
		auto rtCHit = context->CompileShader("ao.rchit", gvk::ShaderMacros(),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			&error);
		auto accumulateShader = context->CompileShader("accumulate.comp", gvk::ShaderMacros(),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			rt_include_directorys, gvk_count_of(rt_include_directorys),
			&error);


		auto vars = deferredPassVert->GetInputVariables().value();
		model.loadFromFile(VKRG_MODEL_DIRECTORY"/sponza/sponza.gltf", context, vars.data(), vars.size(), vkglTF::FileLoadingFlags::RayTracingSupport | vkglTF::FileLoadingFlags::RayTracingOpaque);
		{
			auto [rp, idx] = rg->GetCompiledRenderPassAndSubpass(deferredPassHandle);
			GvkGraphicsPipelineCreateInfo pipelineCI(deferredPassVert, deferredPassFrag, rp, idx);
			pipelineCI.depth_stencil_state.enable_depth_stencil = true;
			deferredPassPipeline = context->CreateGraphicsPipeline(pipelineCI).value();
		}

		{
			gvk::RayTracingPieplineCreateInfo rtCI;
			rtCI.AddRayGenerationShader(rtRayGen.value());
			rtCI.AddRayMissShader(rtMiss.value());
			rtCI.AddRayIntersectionShader(nullptr, nullptr, rtCHit.value());
			rtCI.SetMaxRecursiveDepth(1);

			rtAOPipeline = context->CreateRaytracingPipeline(rtCI).value();
		}


		{
			GvkComputePipelineCreateInfo pipelineCI;
			pipelineCI.shader = accumulateShader.value();

			accumulatePipeline = context->CreateComputePipeline(pipelineCI).value();
		}

		
		auto cameraLayout = deferredPassPipeline->GetInternalLayout(vkglTF::perCameraBindingIndex, (VkShaderStageFlagBits)0);
		auto materialLayout = deferredPassPipeline->GetInternalLayout(vkglTF::perMaterialBindingIndex, (VkShaderStageFlagBits)0);
		auto rtAoLayout = rtAOPipeline->GetInternalLayout(0);
		auto accumulateLayout = accumulatePipeline->GetInternalLayout(0);
		auto accumulateUniformLayout = accumulatePipeline->GetInternalLayout(1);

		model.createDescriptorsForPipeline(descriptorAlloc, deferredPassPipeline);

		//lightData.count = 0;
		IntializeLighting();

		lightBuffer = context->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(lightData), GVK_HOST_WRITE_SEQUENTIAL).value();
		lightBuffer->Write(&lightData, 0, sizeof(lightData));

		cameraBuffer = context->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(CameraUBO), GVK_HOST_WRITE_SEQUENTIAL).value();

		auto cameraUBO = m_Camera.GetCameraUBO();
		aoUniformBuffer.aoTraceDistance = 0.3f;
		aoUniformBuffer.randomSeed = 0.0f;
		aoUniformBuffer.projInverse = cameraUBO.invProjection;
		aoUniformBuffer.viewInverse = cameraUBO.invView;
		aoUniformBuffer.disableAccumulation = 0;

		accUniformBuffer.baseColor = 0;
		accUniformBuffer.applyAO = 1;

		aoGlobalUniformBufferGPU = context->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(cameraUBO), GVK_HOST_WRITE_SEQUENTIAL).value();
		aoGlobalUniformBufferGPU->Write(&aoUniformBuffer, 0, sizeof(aoUniformBuffer));
		accUniformBufferGPU = context->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(accUniformBuffer), GVK_HOST_WRITE_SEQUENTIAL).value();
		accUniformBufferGPU->Write(&accUniformBuffer, 0, sizeof(accUniformBuffer));

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

		// lightDescriptor = descriptorAlloc->Allocate(lightLayout.value()).value();
		cameraDescriptor = descriptorAlloc->Allocate(cameraLayout.value()).value();
		materialDescriptor = descriptorAlloc->Allocate(materialLayout.value()).value();
		// shadingCameraDescriptor = descriptorAlloc->Allocate(shadingCameraLayout.value()).value();
		rtAODescriptor = descriptorAlloc->Allocate(rtAoLayout.value()).value();
		accumulateDescriptor = descriptorAlloc->Allocate(accumulateLayout.value()).value();
		accumulateUniformDescriptor = descriptorAlloc->Allocate(accumulateUniformLayout.value()).value();

		GvkDescriptorSetWrite()
			//.BufferWrite(lightDescriptor, "lightUBO", lightBuffer->GetBuffer(), 0, sizeof(lightData))
			.BufferWrite(cameraDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, sizeof(CameraUBO))
			//.BufferWrite(shadingCameraDescriptor, "cameraUBO", cameraBuffer->GetBuffer(), 0, sizeof(CameraUBO))
			.AccelerationStructureWrite(rtAODescriptor, 0, model.as.tlas->GetAS())
			.BufferWrite(rtAODescriptor, "uni", aoGlobalUniformBufferGPU->GetBuffer(), 0, aoGlobalUniformBufferGPU->GetSize())
			.BufferWrite(accumulateUniformDescriptor, "acc", accUniformBufferGPU->GetBuffer(), 0 , accUniformBufferGPU->GetSize())
			.Emit(context->GetDevice());

		return true;
	}

	void MainLoop(VkCommandBuffer cmd, uint32_t imageIdx) override
	{
		if (window->KeyDown(GVK_KEY_K))
		{
			accUniformBuffer.baseColor = 1 - accUniformBuffer.baseColor;
			accUniformBufferGPU->Write(&accUniformBuffer, 0, sizeof(accUniformBuffer));
		}

		if (window->KeyDown(GVK_KEY_L))
		{
			accUniformBuffer.applyAO = 1 - accUniformBuffer.applyAO;
			accUniformBufferGPU->Write(&accUniformBuffer, 0, sizeof(accUniformBuffer));
		}

		if (window->KeyHold(GVK_KEY_I))
		{
			aoUniformBuffer.aoTraceDistance *= 0.95f;
			printf("aoTraceDistance: %f\n", aoUniformBuffer.aoTraceDistance);
			aoUniformBuffer.disableAccumulation = 1;
		}

		if (window->KeyHold(GVK_KEY_O))
		{
			aoUniformBuffer.aoTraceDistance /= 0.95f;
			printf("aoTraceDistance: %f\n", aoUniformBuffer.aoTraceDistance);
			aoUniformBuffer.disableAccumulation = 1;
		}

		auto cameraUBO = m_Camera.GetCameraUBO();
		aoUniformBuffer.viewInverse = cameraUBO.invView;
		aoUniformBuffer.projInverse = cameraUBO.invProjection;

		aoGlobalUniformBufferGPU->Write(&aoUniformBuffer, 0, sizeof(aoUniformBuffer));

		aoUniformBuffer.randomSeed++;
		rg->Execute(imageIdx, cmd);
		aoUniformBuffer.disableAccumulation = 0;
	}

	RenderPassHandle deferredPassHandle;
	RenderPassHandle rtAOHandle;
	RenderPassHandle accumulateHandle;

	GlobalUniforms aoUniformBuffer;
	AccumulateUniform accUniformBuffer;
	gvk::ptr<gvk::Buffer>   aoGlobalUniformBufferGPU;
	gvk::ptr<gvk::Buffer>   accUniformBufferGPU;

	gvk::ptr<gvk::Pipeline> accumulatePipeline;
	gvk::ptr<gvk::RaytracingPipeline> rtAOPipeline;
	gvk::ptr<gvk::Pipeline> deferredPassPipeline;

	gvk::ptr<gvk::Buffer>			lightBuffer;
	Lights							lightData;
	gvk::ptr<gvk::DescriptorSet>	lightDescriptor;

	gvk::ptr<gvk::Buffer>			cameraBuffer;
	gvk::ptr<gvk::DescriptorSet>    cameraDescriptor;
	gvk::ptr<gvk::DescriptorSet>	shadingCameraDescriptor;

	gvk::ptr<gvk::DescriptorSet>	rtAODescriptor;
	gvk::ptr<gvk::DescriptorSet>	accumulateDescriptor;
	gvk::ptr<gvk::DescriptorSet>	accumulateUniformDescriptor;

	gvk::ptr<gvk::DescriptorSet>	materialDescriptor;
	VkSampler						backBufferSampler;

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

/*
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


	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, app->deferredShadingPipeline->GetPipeline());

	GvkDescriptorSetBindingUpdate(cmd, app->deferredShadingPipeline)
		.BindDescriptorSet(app->shadingCameraDescriptor)
		.BindDescriptorSet(app->lightDescriptor)
		.BindDescriptorSet(app->materialDescriptor)
		.Update();

	vkCmdDraw(cmd, 6, 1, 0, 0);
}
*/


int main()
{
	constexpr uint32_t initWidth = 1000, initHeight = 1000;
	std::make_shared<RTAOApplication>(initWidth, initHeight)->Run();

	return 0;
}

void AccumulatePass::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	if (ctx.CheckAttachmentDirtyFlag(rtTex) || ctx.CheckAttachmentDirtyFlag(gColor))
	{
		VkImageView rtView = ctx.GetImageAttachment(rtTex);
		VkImageView gColorView = ctx.GetImageAttachment(gColor);

		GvkDescriptorSetWrite()
			.ImageWrite(app->accumulateDescriptor, "rtTex", NULL, rtView, VK_IMAGE_LAYOUT_GENERAL)
			.ImageWrite(app->accumulateDescriptor, "gColor", NULL, gColorView, VK_IMAGE_LAYOUT_GENERAL)
		.Emit(app->context->GetDevice());
	}

	VkImageView colorView = ctx.GetImageAttachment(colorOutput);
	GvkDescriptorSetWrite()
		.ImageWrite(app->accumulateDescriptor, "color", NULL, colorView, VK_IMAGE_LAYOUT_GENERAL)
	.Emit(app->context->GetDevice());

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, app->accumulatePipeline->GetPipeline());

	GvkDescriptorSetBindingUpdate(cmd, app->accumulatePipeline)
		.BindDescriptorSet(app->accumulateDescriptor)
		.BindDescriptorSet(app->accumulateUniformDescriptor)
		.Update();

	vkCmdDispatch(cmd, app->windowWidth, app->windowHeight, 1);
}

void RTPass::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
{
	
	
	if (ctx.CheckAttachmentDirtyFlag(normal) || ctx.CheckAttachmentDirtyFlag(depth) || ctx.CheckAttachmentDirtyFlag(rtTex))
	{
		VkImageView rtTexView = ctx.GetImageAttachment(rtTex);
		VkImageView depthView = ctx.GetImageAttachment(depth);
		VkImageView normalView = ctx.GetImageAttachment(normal);

		GvkDescriptorSetWrite()
			.ImageWrite(app->rtAODescriptor, "image", NULL, rtTexView, VK_IMAGE_LAYOUT_GENERAL)
			.ImageWrite(app->rtAODescriptor, "normalTex", NULL, normalView, VK_IMAGE_LAYOUT_GENERAL)
			.ImageWrite(app->rtAODescriptor, "depthTex", app->backBufferSampler, depthView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		.Emit(app->context->GetDevice());
	}

	GvkDescriptorSetBindingUpdate(cmd, app->rtAOPipeline)
		.BindDescriptorSet(app->rtAODescriptor)
		.Update();

	app->rtAOPipeline->TraceRay(cmd, app->windowWidth, app->windowHeight, 1);
}
