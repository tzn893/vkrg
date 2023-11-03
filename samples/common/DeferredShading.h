#pragma once
#include "vkrg/pass.h"
using namespace vkrg;

class DeferredShading : public RenderPassInterface
{
public:
	DeferredShading(RenderPass* targetPass, RenderPassAttachment normal, RenderPassAttachment color,
		RenderPassAttachment material, RenderPassAttachment color_output, RenderPassAttachment depth);

	virtual void GetClearValue(uint32_t attachment, VkClearValue& value) {}

	virtual VkImageLayout GetAttachmentExpectedState(uint32_t attachment) override;

	virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
		VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp) override;

	virtual RenderPassType ExpectedType() override { return RenderPassType::Graphics; }

	virtual bool OnValidationCheck(std::string& msg) override;
	
protected:
	bool CheckAttachment(RenderPassAttachment attachment, std::string attachment_name, std::string& msg);

	RenderPassAttachment normal;
	RenderPassAttachment color;
	RenderPassAttachment material;
	RenderPassAttachment color_output;
	RenderPassAttachment depth;

};

