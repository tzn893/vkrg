#pragma once
#include "vkrg/pass.h"
using namespace vkrg;

class DeferredShading : public RenderPass
{
public:
	DeferredShading(const std::string& name);

	void Compile(RenderPassPrototypeInfoCollector& collector) override;

	void Execute() override;

	const char* GetPrototypeName() override;
private:
	uint32_t m_gbufferColor;
	uint32_t m_gbufferNormalDepth;
	uint32_t m_gbufferMaterial;
	uint32_t m_depthBuffer;
};

REGISTER_RENDER_PASS_PROTOTYPE(DeferredShading);
