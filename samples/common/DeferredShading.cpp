#include "DeferredShading.h"


DeferredShading::DeferredShading(RenderPass* targetPass, RenderPassAttachment normal, RenderPassAttachment color, RenderPassAttachment material, RenderPassAttachment color_output, RenderPassAttachment depth)
	: RenderPassInterface(targetPass), normal(normal), color(color), material(material), color_output(color_output), depth(depth)
{
}


void DeferredShading::GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp, VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp)
{
	if (attachment == color_output.idx)
	{
		loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}

	stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
}


bool DeferredShading::OnValidationCheck(std::string& msg)
{
	
	if (!CheckAttachment(color, "color", msg))
	{
		return false;
	}
	if (!CheckAttachment(normal, "normalDepth", msg))
	{
		return false;
	}
	if (!CheckAttachment(material, "material", msg))
	{
		return false;
	}

	if (depth.targetPass != m_TargetPass)
	{
		return false;
	}
	if (depth.range.imageRange.aspectMask != VK_IMAGE_ASPECT_DEPTH_BIT)
	{
		return false;
	}


	if (color_output.type != RenderPassAttachment::ImageColorOutput)
	{
		msg = std::string("invalid attachment ") + "color_output" + " attachment for deferred shading pass";
		return false;
	}
	
	return true;
}

bool  DeferredShading::CheckAttachment(RenderPassAttachment attachment, std::string attachment_name, std::string& msg)
{
	if (attachment.targetPass != m_TargetPass)
	{
		msg = std::string("invalid attachment ") + attachment_name + " deferred shading pass interface must be binded to the same render pass as this attachment's";
		return false;
	}
	if (attachment.type != RenderPassAttachment::ImageColorInput)
	{
		msg = std::string("invalid ") + attachment_name + " attachment for deferred shading pass";
		return false;
	}
	if (m_TargetPass->GetAttachmentInfo(attachment).format != VK_FORMAT_R8G8B8A8_UNORM)
	{
		msg = std::string("invalid format for ") + attachment_name + " attachment : expected VK_FORMAT_R8G8B8A8_UNORM";
		return false;
	}

	return true;
}