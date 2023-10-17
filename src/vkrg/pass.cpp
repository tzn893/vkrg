#include "vkrg/pass.h"
#include "vkrg/graph.h"
#include <unordered_map>


namespace vkrg {
	
	RenderPass::RenderPass(RenderGraph* graph, const char* name, RenderPassType type)
		: m_Graph(graph), name(name), m_RenderPassType(type)
	{}

	bool CheckAttachmentCompality(ImageSlice range, ResourceInfo info)
	{
		if (info.extType == ResourceExtensionType::Buffer) return false;
		if ((gvk::GetAllAspects(info.format) & range.aspectMask) != range.aspectMask) return false;

		if (range.baseArrayLayer + range.layerCount > info.channelCount) return false;
		if (range.baseMipLevel + range.levelCount > info.mipCount) return false;
		return true;
	}

	bool CheckAttachmentCompality(BufferSlice range, ResourceInfo info)
	{
		if (info.extType != ResourceExtensionType::Buffer) return false;

		if (range.offset + range.size > info.ext.buffer.size) return false;
		return true;
	}

	opt<RenderPassAttachment> RenderPass::AddImageColorOutput(const char* name, ImageSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if (range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) return std::nullopt;
		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageColorOutput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageDepthOutput(const char* name, ImageSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if ((range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) == 0) return std::nullopt;
		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageDepthOutput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageColorInput(const char* name, ImageSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageColorInput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageStorageInput(const char* name, ImageSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageStorageInput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageStorageOutput(const char* name, ImageSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageStorageOutput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddBufferStorageInput(const char* name, BufferSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::BufferStorageInput;
		attachment.range.bufferRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddBufferStorageOutput(const char* name, BufferSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::BufferStorageOutput;
		attachment.range.bufferRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddBufferInput(const char* name, BufferSlice range)
	{
		ResourceHandle handle;
		if (auto res = m_Graph->FindGraphResource(name); res.has_value())
		{
			handle = res.value();
		}
		else
		{
			return std::nullopt;
		}

		if (CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))) return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::BufferInput;
		attachment.range.bufferRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	void RenderPass::AttachInterface(ptr<RenderPassInterface> inter)
	{
		m_RenderPassInterface = inter;
	}

	ResourceInfo RenderPass::GetAttachmentInfo(const RenderPassAttachment& idx)
	{
		vkrg_assert(idx.targetPass == this);
		return m_Graph->GetResourceInfo(m_AttachmentResourceHandle[idx.idx]);
	}

	void RenderPass::GetAttachmentOperationState(const RenderPassAttachment& idx, RenderPassAttachmentOperationState& state)
	{
		vkrg_assert(idx.targetPass == this);
		m_RenderPassInterface->GetAttachmentStoreLoadOperation(idx.idx, state.load, state.store, state.stencilLoad, state.stencilStore);
	}

	VkImageLayout RenderPass::GetAttachmentExpectedState(const RenderPassAttachment& idx)
	{
		return  m_RenderPassInterface->GetAttachmentExpectedState(idx.idx);
	}

	bool RenderPass::RequireClearColor(const RenderPassAttachment& idx)
	{

		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, _1;
		VkAttachmentStoreOp _2, _3;

		m_RenderPassInterface->GetAttachmentStoreLoadOperation(idx.idx, loadOp, _2, _1, _3);

		return loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
	}

	void RenderPass::GetClearColor(const RenderPassAttachment& idx, VkClearValue& clearValue)
	{
		m_RenderPassInterface->GetClearValue(idx.idx, clearValue);
	}

	bool RenderPass::ValidationCheck(std::string& msg)
	{
		if (m_RenderPassInterface == nullptr)
		{
			msg = "no render pass interface attached to render pass";
			return false;
		}

		if (m_RenderPassInterface->ExpectedType() != m_RenderPassType)
		{
			msg = "render pass interface's type doesn't match render pass's type";
			return false;
		}

		for (auto& attachment : m_Attachments)
		{
			if (attachment.type == RenderPassAttachment::ImageStorageInput || attachment.type == RenderPassAttachment::BufferStorageInput
				|| attachment.type == RenderPassAttachment::BufferStorageOutput || attachment.type == RenderPassAttachment::ImageStorageOutput)
			{
				if (m_RenderPassType != RenderPassType::Compute)
				{
					msg = "invalid attachment " + std::to_string(attachment.idx) + " storage image/buffer for graphics render pass";
					return false;
				}
			}
			if (attachment.type == RenderPassAttachment::ImageColorOutput)
			{
				if (m_RenderPassInterface->GetAttachmentExpectedState(attachment.idx) != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
				{
					msg = "invalid attachment format " + std::to_string(attachment.idx) + " expected VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL";
					return false;
				}
			}
			if (attachment.type == RenderPassAttachment::ImageDepthOutput)
			{
				if (m_RenderPassInterface->GetAttachmentExpectedState(attachment.idx) != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
				{
					msg = "invalid attachment format " + std::to_string(attachment.idx) + " expected VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL";
					return false;
				}
			}
		}


		return m_RenderPassInterface->OnValidationCheck(msg);
	}

	const char* RenderPass::GetName()
	{
		return name.c_str();
	}
	
	const std::vector<ResourceHandle>& RenderPass::GetAttachedResourceHandles()
	{
		return m_AttachmentResourceHandle;
	}

	const std::vector<RenderPassAttachment>& RenderPass::GetAttachments()
	{
		return m_Attachments;
	}
	bool RenderPassAttachment::WriteToResource() const
	{
		return type == RenderPassAttachment::ImageColorOutput || type == RenderPassAttachment::ImageDepthOutput || type == RenderPassAttachment::ImageStorageOutput
			|| type == RenderPassAttachment::BufferStorageOutput;
	}

	bool RenderPassAttachment::ReadFromResource() const
	{
		return type == RenderPassAttachment::ImageColorInput || type == RenderPassAttachment::BufferStorageInput || type == RenderPassAttachment::ImageStorageInput;
	}

	bool RenderPassAttachment::IsImage() const
	{
		return type == RenderPassAttachment::ImageColorInput || type == RenderPassAttachment::ImageDepthOutput || type == RenderPassAttachment::ImageStorageOutput
			|| type == RenderPassAttachment::ImageColorOutput || type == RenderPassAttachment::ImageStorageInput;
	}

	bool RenderPassAttachment::IsBuffer() const
	{
		return type == RenderPassAttachment::BufferInput || type == RenderPassAttachment::BufferStorageInput || type == RenderPassAttachment::BufferStorageOutput;
	}

	RenderPassType RenderPass::GetType()
	{
		return m_RenderPassType;
	}
}
