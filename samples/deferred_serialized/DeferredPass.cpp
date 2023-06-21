#include "DeferredPass.h"

REGISTER_RENDER_PASS_CREATE(DeferredPass);

DeferredPass::DeferredPass(const std::string& name): RenderPass(name)
{

}

void DeferredPass::Compile(RenderPassPrototypeInfoCollector& collector)
{
	m_gbufferColor = collector.OutputScaleByScreen("gbuffer-color", VKRG_LAYOUT_TEXTURE2D, VKRG_FORMAT_RGBA8, 1, 1);
	m_gbufferNormalDepth = collector.OutputScaleByScreen("gbuffer-normal-depth", VKRG_LAYOUT_TEXTURE2D, VKRG_FORMAT_RGBA8, 1, 1);
	m_gbufferMaterial = collector.OutputScaleByScreen("gbuffer-material", VKRG_LAYOUT_TEXTURE2D, VKRG_FORMAT_RGBA8, 1, 1);
	m_depthBuffer = collector.OutputScaleByScreen("depth-buffer", VKRG_LAYOUT_TEXTURE2D, VKRG_FORMAT_D24S8, 1, 1);
}

void DeferredPass::Execute()
{
	printf("execute deferred render pass %s\n", GetName());
}
