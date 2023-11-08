#pragma once
#include "vkrg/pass.h"
using namespace vkrg;

class DeferredPass : public RenderPassInterface
{
public:
	DeferredPass(RenderPass* targetPass, RenderPassAttachment normalDepth, RenderPassAttachment color, 
		RenderPassAttachment material, RenderPassAttachment depthStencil);

	virtual void GetClearValue(uint32_t attachment, VkClearValue& value) override;

	virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
		VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp) override;

	virtual RenderPassType ExpectedType() override { return RenderPassType::Graphics; }

	virtual bool OnValidationCheck(std::string& msg) override;

protected:
	bool CheckAttachment(RenderPassAttachment attachment,std::string attachment_name, std::string& msg);

	RenderPassAttachment normalDepth;
	RenderPassAttachment color;
	RenderPassAttachment material;
	RenderPassAttachment depthStencil;
};
