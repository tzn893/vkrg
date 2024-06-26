#include "vkrg/pass.h"
#include "vkrg/graph.h"
#include <unordered_map>


namespace vkrg {

	RenderPass::RenderPass(RenderGraph* graph, const char* name, RenderPassType type, RenderPassExtension expectedExtension)
		: m_Graph(graph), name(name), m_RenderPassType(type), m_ExpectedExtension(expectedExtension)
	{}

	bool CheckAttachmentCompality(ImageSlice range, ResourceInfo info)
	{
		if ((gvk::GetAllAspects(info.format) & range.aspectMask) != range.aspectMask) return false;

		if (range.baseArrayLayer + range.layerCount > info.channelCount) return false;
		if (range.baseMipLevel + range.levelCount > info.mipCount) return false;
		return true;
	}

	bool CheckAttachmentCompality(RenderPassExtension expectedExtension, ImageSlice range, ResourceInfo info)
	{
		if (info.extType == ResourceExtensionType::Buffer) return false;

		if (expectedExtension.extensionType != info.extType) return false;
		if (expectedExtension.extensionType == ResourceExtensionType::Fixed)
		{
			return expectedExtension.extension.fixed.x == info.ext.fixed.x &&
				expectedExtension.extension.fixed.y == info.ext.fixed.y &&
				expectedExtension.extension.fixed.z == info.ext.fixed.z;
		}
		else if (expectedExtension.extensionType == ResourceExtensionType::Screen)
		{
			return vkrg_fequal(expectedExtension.extension.screen.x, info.ext.screen.x) &&
				vkrg_fequal(expectedExtension.extension.screen.y, info.ext.screen.y);
		}

		return CheckAttachmentCompality(range, info);
	}

	bool CheckAttachmentCompality(BufferSlice range, ResourceInfo info)
	{
		if (info.extType != ResourceExtensionType::Buffer) return false;

		if (range.offset + range.size > info.ext.buffer.size) return false;
		return true;
	}

	bool CheckAttachmentViewCompality(ResourceInfo& info, ImageSlice slice, VkImageViewType type)
	{
		if (info.IsBuffer()) return false;

		bool onedCompatiable = info.extType != ResourceExtensionType::Screen && (info.ext.fixed.y <= 1) && (info.ext.fixed.z <= 1);
		bool twoCompatiable = (info.extType == ResourceExtensionType::Screen || info.ext.fixed.z <= 1);
		bool thirdCompatiable = (info.extType != ResourceExtensionType::Screen);
		bool arrayCompatible = (slice.layerCount > 1);
		bool cubeCompatible = (slice.layerCount == 6);

		if (type == VK_IMAGE_VIEW_TYPE_1D)
		{
			return onedCompatiable && !arrayCompatible;
		}
		if (type == VK_IMAGE_VIEW_TYPE_2D)
		{
			return twoCompatiable && !arrayCompatible;
		}
		if (type == VK_IMAGE_VIEW_TYPE_3D)
		{
			return thirdCompatiable && !arrayCompatible;
		}
		if (type == VK_IMAGE_VIEW_TYPE_1D_ARRAY)
		{
			return onedCompatiable && arrayCompatible;
		}
		if (type == VK_IMAGE_VIEW_TYPE_2D_ARRAY)
		{
			return twoCompatiable && arrayCompatible;
		}
		if (type == VK_IMAGE_VIEW_TYPE_CUBE)
		{
			return twoCompatiable && cubeCompatible;
		}
		return false;
	}

	opt<RenderPassAttachment> RenderPass::AddImageColorOutput(const char* name, ImageSlice range, VkImageViewType    viewType)
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

		if (m_RenderPassType != RenderPassType::Graphics)
		{
			return std::nullopt;
		}

		if (range.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT) return std::nullopt;

		if (!CheckAttachmentCompality(m_ExpectedExtension, range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(m_Graph->GetResourceInfo(handle), range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageColorOutput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageDepthOutput(const char* name, ImageSlice range, VkImageViewType    viewType)
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

		if (m_RenderPassType != RenderPassType::Graphics)
		{
			return std::nullopt;
		}

		ResourceInfo resInfo = m_Graph->GetResourceInfo(handle);

		if ((range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) == 0) return std::nullopt;
		if (!CheckAttachmentCompality(m_ExpectedExtension, range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(resInfo, range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageDepthOutput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	/*
	opt<RenderPassAttachment> RenderPass::AddImageDepthInput(const char* name, ImageSlice range, VkImageViewType viewType)
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

		if (m_RenderPassType == RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		ResourceInfo resInfo = m_Graph->GetResourceInfo(handle);

		if ((range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) == 0) return std::nullopt;
		if (!CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(resInfo, range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageDepthInput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}
	*/
	opt<RenderPassAttachment> RenderPass::AddImageColorInput(const char* name, ImageSlice range, VkImageViewType    viewType)
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

		if (m_RenderPassType == RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		if (!CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(m_Graph->GetResourceInfo(handle), range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageColorInput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageStorageInput(const char* name, ImageSlice range, VkImageViewType  viewType)
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

		if (m_RenderPassType == RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		if (!CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(m_Graph->GetResourceInfo(handle), range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageStorageInput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageStorageOutput(const char* name, ImageSlice range, VkImageViewType    viewType)
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

		if (m_RenderPassType == RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		if (!CheckAttachmentCompality(m_ExpectedExtension, range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(m_Graph->GetResourceInfo(handle), range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageStorageOutput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageRTSampledInput(const char* name, ImageSlice range, VkImageViewType viewType)
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

		if (m_RenderPassType != RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		if (!CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(m_Graph->GetResourceInfo(handle), range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageRTSampledInput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageRTInput(const char* name, ImageSlice range, VkImageViewType viewType)
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

		if (m_RenderPassType != RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		if (!CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(m_Graph->GetResourceInfo(handle), range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageRTInput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddImageRTOutput(const char* name, ImageSlice range, VkImageViewType viewType)
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

		if (m_RenderPassType != RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		if (!CheckAttachmentCompality(range, m_Graph->GetResourceInfo(handle))
			|| !CheckAttachmentViewCompality(m_Graph->GetResourceInfo(handle), range, viewType))
			return std::nullopt;

		RenderPassAttachment attachment;
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::ImageRTOutput;
		attachment.range.imageRange = range;
		attachment.targetPass = this;
		attachment.viewType = viewType;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddBufferRTInput(const char* name, BufferSlice range)
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

		if (m_RenderPassType != RenderPassType::Raytracing)
		{
			return std::nullopt;
		}


		auto resInfo = m_Graph->GetResourceInfo(handle);
		if (range.size == 0xffffffff)
		{
			range.size = resInfo.ext.buffer.size;
		}

		if (!CheckAttachmentCompality(range, resInfo)) return std::nullopt;

		RenderPassAttachment attachment{};
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::BufferRTInput;
		attachment.range.bufferRange = range;
		attachment.targetPass = this;

		m_Attachments.push_back(attachment);
		m_AttachmentResourceHandle.push_back(handle);

		return attachment;
	}

	opt<RenderPassAttachment> RenderPass::AddBufferRTOutput(const char* name, BufferSlice range)
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

		if (m_RenderPassType != RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		auto resInfo = m_Graph->GetResourceInfo(handle);
		if (range.size == 0xffffffff)
		{
			range.size = resInfo.ext.buffer.size;
		}

		if (!CheckAttachmentCompality(range, resInfo)) return std::nullopt;

		RenderPassAttachment attachment{};
		attachment.idx = m_Attachments.size();
		attachment.type = RenderPassAttachment::BufferRTOutput;
		attachment.range.bufferRange = range;
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

		auto resInfo = m_Graph->GetResourceInfo(handle);
		if (range.size == 0xffffffff)
		{
			range.size = resInfo.ext.buffer.size;
		}

		if (m_RenderPassType == RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		if (!CheckAttachmentCompality(range, resInfo)) return std::nullopt;

		RenderPassAttachment attachment{};
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

		if (m_RenderPassType == RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		auto resInfo = m_Graph->GetResourceInfo(handle);
		if (range.size == 0xffffffff)
		{
			range.size = resInfo.ext.buffer.size;
		}

		if (!CheckAttachmentCompality(range, resInfo)) return std::nullopt;

		RenderPassAttachment attachment{};
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

		if (m_RenderPassType == RenderPassType::Raytracing)
		{
			return std::nullopt;
		}

		auto resInfo = m_Graph->GetResourceInfo(handle);
		if (range.size == 0xffffffff)
		{
			range.size = resInfo.ext.buffer.size;
		}

		if (!CheckAttachmentCompality(range, resInfo)) return std::nullopt;

		RenderPassAttachment attachment{};
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
		VkImageLayout initGuess = VK_IMAGE_LAYOUT_UNDEFINED;
		if (idx.type == RenderPassAttachment::Type::ImageColorOutput)
		{
			initGuess = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}
		else if (idx.type == RenderPassAttachment::Type::ImageColorInput
			|| idx.type == RenderPassAttachment::Type::ImageRTSampledInput)
		{
			initGuess = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		}
		else if (idx.type == RenderPassAttachment::Type::ImageDepthOutput)
		{
			initGuess = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		else if (idx.type == RenderPassAttachment::Type::ImageStorageOutput
			|| idx.type == RenderPassAttachment::Type::ImageRTOutput)
		{
			initGuess = VK_IMAGE_LAYOUT_GENERAL;
		}
		/*
		The image subresources for a storage image must be in the VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR or
		VK_IMAGE_LAYOUT_GENERAL layout in order to access its data in a shader.
		*/
		else if (idx.type == RenderPassAttachment::Type::ImageStorageInput ||
			idx.type == RenderPassAttachment::Type::ImageRTInput)
		{
			// initGuess = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
			initGuess = VK_IMAGE_LAYOUT_GENERAL;
		}
		else if (idx.type == RenderPassAttachment::Type::ImageDepthInput)
		{
			initGuess = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}

		m_RenderPassInterface->GetAttachmentExpectedState(idx.idx, initGuess);

		return initGuess;
	}

	bool RenderPass::RequireClearColor(const RenderPassAttachment& idx)
	{

		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, _1;
		VkAttachmentStoreOp _2, _3;

		if (m_RenderPassType == RenderPassType::Graphics)
		{
			m_RenderPassInterface->GetAttachmentStoreLoadOperation(idx.idx, loadOp, _2, _1, _3);
		}

		return loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR;
	}

	void RenderPass::GetClearColor(const RenderPassAttachment& idx, VkClearValue& clearValue)
	{
		m_RenderPassInterface->GetClearValue(idx.idx, clearValue);
	}

	void RenderPass::OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd)
	{
		m_RenderPassInterface->OnRender(ctx, cmd);
	}

	RenderPassExtension RenderPass::GetRenderPassExtension()
	{
		return m_ExpectedExtension;
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
			if (attachment.type == RenderPassAttachment::BufferStorageOutput || attachment.type == RenderPassAttachment::ImageStorageOutput)
			{
				if (m_RenderPassType != RenderPassType::Compute)
				{
					msg = "invalid attachment " + std::to_string(attachment.idx) + " storage image/buffer for graphics render pass";
					return false;
				}
			}
			if (attachment.type == RenderPassAttachment::ImageColorOutput)
			{
				if (GetAttachmentExpectedState(attachment) != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
				{
					msg = "invalid attachment format " + std::to_string(attachment.idx) + " expected VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL";
					return false;
				}
			}
			if (attachment.type == RenderPassAttachment::ImageDepthOutput)
			{
				if (GetAttachmentExpectedState(attachment) != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
				{
					msg = "invalid attachment format " + std::to_string(attachment.idx) + " expected VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
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
			|| type == RenderPassAttachment::BufferStorageOutput || type == RenderPassAttachment::ImageRTOutput;
	}

	bool RenderPassAttachment::ReadFromResource() const
	{
		return type == RenderPassAttachment::ImageColorInput || type == RenderPassAttachment::BufferStorageInput || type == RenderPassAttachment::ImageStorageInput
			|| type == RenderPassAttachment::ImageDepthInput || type == RenderPassAttachment::ImageRTInput || type == RenderPassAttachment::ImageRTSampledInput;
	}

	bool RenderPassAttachment::IsImage() const
	{
		return type == RenderPassAttachment::ImageColorInput || type == RenderPassAttachment::ImageDepthOutput || type == RenderPassAttachment::ImageStorageOutput
			|| type == RenderPassAttachment::ImageColorOutput || type == RenderPassAttachment::ImageStorageInput || type == RenderPassAttachment::ImageRTSampledInput
			|| type == RenderPassAttachment::ImageRTInput || type == RenderPassAttachment::ImageRTOutput;
	}

	bool RenderPassAttachment::IsBuffer() const
	{
		return type == RenderPassAttachment::BufferInput || type == RenderPassAttachment::BufferStorageInput || type == RenderPassAttachment::BufferStorageOutput
			|| type == RenderPassAttachment::BufferRTInput || type == RenderPassAttachment::BufferRTOutput;
	}

	RenderPassType RenderPass::GetType()
	{
		return m_RenderPassType;
	}
	bool RenderPass::IsGeneralPass()
	{
		return GetType() != RenderPassType::Graphics;
	}
	RenderPassRuntimeContext::RenderPassRuntimeContext(RenderGraph* graph, uint32_t frameIdx, uint32_t passIdx)
		:m_Graph(graph), m_FrameIdx(frameIdx), m_passIdx(passIdx)
	{
		auto [mergedPass, _] = m_Graph->FindInvolvedMergedPass(m_Graph->m_RenderPassNodeList[m_passIdx]).value();
		m_mergedPassIdx = mergedPass->idx;
	}
	VkImageView RenderPassRuntimeContext::GetImageAttachment(RenderPassAttachment attachment, uint32_t frameIdx)
	{
		if (frameIdx == 0xffffffff) frameIdx = m_FrameIdx;

		vkrg_assert(frameIdx < RenderGraph::maxFrameOnFlightCount);
		vkrg_assert(attachment.targetPass == m_Graph->m_RenderPassList[m_passIdx].pass.get());

		auto view = m_Graph->m_RPViewTable[m_passIdx].attachmentViews[frameIdx][attachment.idx];
		vkrg_assert(view.isImage);

		return view.imageView;
	}

	BufferView RenderPassRuntimeContext::GetBufferAttachment(RenderPassAttachment attachment, uint32_t frameIdx)
	{
		if (frameIdx == 0xffffffff) frameIdx = m_FrameIdx;

		vkrg_assert(frameIdx < RenderGraph::maxFrameOnFlightCount);
		vkrg_assert(attachment.targetPass == m_Graph->m_RenderPassList[m_passIdx].pass.get());

		auto view = m_Graph->m_RPViewTable[m_passIdx].attachmentViews[frameIdx][attachment.idx];
		vkrg_assert(!view.isImage);

		return view.bufferView;
	}


	ptr<gvk::Image> RenderPassRuntimeContext::GetImage(RenderPassAttachment attachment, uint32_t frameIdx /*= 0xffffffff*/)
	{
		if (frameIdx == 0xffffffff) frameIdx = m_FrameIdx;
		if (m_Graph->m_Options.disableFrameOnFlight) frameIdx = 0;

		vkrg_assert(frameIdx < RenderGraph::maxFrameOnFlightCount);
		vkrg_assert(attachment.targetPass == m_Graph->m_RenderPassList[m_passIdx].pass.get());

		ResourceHandle resource = m_Graph->m_RenderPassList[m_passIdx].pass->GetAttachedResourceHandles()[attachment.idx];
		if (resource.external)
		{
			uint32_t externalIdx = m_Graph->m_LogicalResourceAssignmentTable[resource.idx].idx;
			return m_Graph->m_ExternalResourceBindings[externalIdx].images[frameIdx];
		}
		else
		{
			uint32_t physicalIdx = m_Graph->m_LogicalResourceAssignmentTable[resource.idx].idx;
			return m_Graph->m_PhysicalResourceBindings[physicalIdx].images[frameIdx];
		}

		return nullptr;
	}

	VkImageLayout RenderPassRuntimeContext::GetImageLayout(RenderPassAttachment attachment)
	{
		vkrg_assert(attachment.targetPass == m_Graph->m_RenderPassList[m_passIdx].pass.get());

		return m_Graph->m_RenderPassList[m_passIdx].pass->GetAttachmentExpectedState(attachment);
	}

	VkImageSubresourceRange RenderPassRuntimeContext::GetImageViewRange(RenderPassAttachment attachment)
	{
		return attachment.range.imageRange;
	}

	VkExtent3D RenderPassRuntimeContext::GetImageExtent(RenderPassAttachment attachment, uint32_t mipLevel)
	{
		ResourceHandle resource = m_Graph->m_RenderPassList[m_passIdx].pass->GetAttachedResourceHandles()[attachment.idx];

		ResourceInfo& info = m_Graph->m_LogicalResourceList[resource.idx].info;
		auto [w, h, d] = m_Graph->GetExpectedExtension(info.ext, info.extType);
		return VkExtent3D{ w >> mipLevel, h >> mipLevel ,d };
	}

	// TODO dirty flag for every view
	bool RenderPassRuntimeContext::CheckAttachmentDirtyFlag(RenderPassAttachment attachment)
	{
		auto& rp = m_Graph->m_RenderPassList[m_passIdx];
		auto& resource = rp.pass->m_AttachmentResourceHandle[attachment.idx];

		auto& resourceAssignment = m_Graph->m_LogicalResourceAssignmentTable[resource.idx];
		return m_Graph->GetAssignedResourceBinding(resourceAssignment)->dirtyFlag;
	}
}
