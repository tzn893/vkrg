#include "DeferredPass.h"

REGISTER_EXECUTABLE_PASS_CREATE(DeferredPass);

DeferredPass::DeferredPass(const std::string& name): ExecutablePass(name)
{

}

void DeferredPass::GeneratePrototypeInfo(ExecutablePassPrototypeInfoCollector& collector)
{
	collector.SetType(VKRG_RP_TYPE_RENDER_PASS);

	m_gbufferColor = collector.OutputScaleByScreen("gbuffer-color", VKRG_FORMAT_RGBA8, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	m_gbufferNormalDepth = collector.OutputScaleByScreen("gbuffer-normal-depth", VKRG_FORMAT_RGBA8, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	m_gbufferMaterial = collector.OutputScaleByScreen("gbuffer-material", VKRG_FORMAT_RGBA8, 1, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
	m_depthBuffer = collector.OutputScaleByScreen("depth-buffer", VKRG_FORMAT_D24S8, 1, 1, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void DeferredPass::Execute()
{
	printf("execute deferred render pass %s\n", GetName());
}

VKRG_RENDER_PASS_TYPE DeferredPass::GetType()
{
	return VKRG_RP_TYPE_RENDER_PASS;
}