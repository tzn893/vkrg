#pragma once
#include "vkrg/pass.h"
using namespace vkrg;

class DeferredPass : public RenderPassInterface
{
public:
	DeferredPass(RenderPass* targetPass, RenderPassAttachment normalDepth, RenderPassAttachment color, 
		RenderPassAttachment material, RenderPassAttachment depthStencil, RenderPassAttachment position);

	virtual void GetClearValue(uint32_t attachment, VkClearValue& value) override;

	virtual void OnRender() override;

	virtual bool OnValidationCheck(std::string& msg) override;

private:
	bool CheckAttachment(RenderPassAttachment attachment,std::string attachment_name, std::string& msg);

	RenderPassAttachment normalDepth;
	RenderPassAttachment color;
	RenderPassAttachment material;
	RenderPassAttachment position;
	RenderPassAttachment depthStencil;
};
