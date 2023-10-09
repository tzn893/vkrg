#pragma once
#include "vkrg/pass.h"
using namespace vkrg;

class DeferredShading : public ExecutablePass
{
public:
	DeferredShading(const std::string& name);

	void GeneratePrototypeInfo(ExecutablePassPrototypeInfoCollector& collector) override;

	void Execute() override;

	const char* GetPrototypeName() override;

	VKRG_RENDER_PASS_TYPE GetType() override;
private:
	uint32_t m_gbufferColor;
	uint32_t m_gbufferNormalDepth;
	uint32_t m_gbufferMaterial;
	uint32_t m_depthBuffer;
};

REGISTER_EXECUTABLE_PASS_PROTOTYPE(DeferredShading);
