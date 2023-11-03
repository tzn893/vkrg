#include "DeferredPass.h"


DeferredPass::DeferredPass(RenderPass* targetPass, RenderPassAttachment normalDepth, RenderPassAttachment color,
	RenderPassAttachment material, RenderPassAttachment depthStencil)
	:
	RenderPassInterface(targetPass), normalDepth(normalDepth), color(color),
		material(material), depthStencil(depthStencil)
{}

void DeferredPass::GetClearValue(uint32_t attachment, VkClearValue& value)
{
	if (attachment == depthStencil.idx)
	{
		value.depthStencil.depth = 1;
		value.depthStencil.stencil = 0;
	}
	else
	{
		value.color = { 0, 0, 0, 1 };
	}
}

VkImageLayout DeferredPass::GetAttachmentExpectedState(uint32_t attachment)
{
	if (attachment == depthStencil.idx)
	{
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	
	return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
}

void DeferredPass::GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp, VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp)
{
	loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

bool DeferredPass::OnValidationCheck(std::string& msg)
{
	if (depthStencil.type != RenderPassAttachment::ImageDepthOutput)
	{
		msg = "invalid depth stencil attachment for deferred render pass";
		return false;
	}
	
	if (!CheckAttachment(normalDepth, "normalDepth", msg))
	{
		return false;
	}
	if (!CheckAttachment(color, "color", msg))
	{
		return false;
	}
	if (!CheckAttachment(material, "material", msg))
	{
		return false;
	}

	return true;
}

bool DeferredPass::CheckAttachment(RenderPassAttachment attachment, std::string attachment_name, std::string& msg)
{

	if (attachment.targetPass != m_TargetPass)
	{
		msg = std::string("invalid attachment ") + attachment_name + " deferred render pass interface must be binded to the same render pass as this attachment's";
		return false;
	}
	if (attachment.type != RenderPassAttachment::ImageColorOutput)
	{
		msg = std::string("invalid ") + attachment_name + " attachment for deferred render pass";
		return false;
	}
	if (m_TargetPass->GetAttachmentInfo(attachment).format != VK_FORMAT_R8G8B8A8_UNORM)
	{
		msg = std::string("invalid format for ") + attachment_name +" attachment : expected VK_FORMAT_R8G8B8A8_UNORM";
		return false;
	}

	return true;
}
