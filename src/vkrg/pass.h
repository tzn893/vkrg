#pragma once
#include "vkrg/common.h"
#include "vkrg/resource.h"

namespace vkrg
{
	
	class RenderGraph;
	struct ResourceHandle;
	class RenderPass;

	struct RenderPassAttachmentOperationState
	{
		VkAttachmentLoadOp  load;
		VkAttachmentStoreOp store;
		VkAttachmentLoadOp  stencilLoad;
		VkAttachmentStoreOp stencilStore;
	};

	struct RenderPassAttachment
	{
		enum Type
		{
			ImageColorOutput,
			ImageDepthOutput,
			ImageDepthInput,
			ImageColorInput,

			ImageStorageInput,
			ImageStorageOutput,

			BufferStorageInput,
			BufferStorageOutput,
			BufferInput
		} type;

		VkImageViewType    viewType;

		uint32_t idx;
		union
		{
			ImageSlice imageRange;
			BufferSlice bufferRange;
		} range;
		RenderPass* targetPass;

		bool WriteToResource() const;
		bool ReadFromResource() const;

		bool IsImage() const;
		bool IsBuffer() const;
	};

	class RenderPassInterface;

	enum class RenderPassType
	{
		Graphics,
		Compute
	};


	struct RenderPassExtension
	{
		RenderPassExtension()
		{
			extensionType = ResourceExtensionType::Screen;
			extension.screen.x = 1;
			extension.screen.y = 1;
		}

		ResourceInfo::Extension extension;
		ResourceExtensionType   extensionType; 

		bool operator==(const RenderPassExtension& other) const
		{
			if (extensionType != other.extensionType) return false;
			if (extensionType == ResourceExtensionType::Fixed)
			{
				return extension.fixed.x == other.extension.fixed.x &&
					extension.fixed.y == other.extension.fixed.y &&
					extension.fixed.z == other.extension.fixed.z;
			}
			if (extensionType == ResourceExtensionType::Screen)
			{
				return vkrg_fequal(extension.screen.x, other.extension.screen.x) &&
					vkrg_fequal(extension.screen.y, other.extension.screen.y);
			}
			return false;
		}
	};

	
	struct BufferView
	{
		VkBuffer buffer;
		uint64_t offset;
		uint64_t size;
	};

	class RenderPassRuntimeContext
	{
	public:
		RenderPassRuntimeContext(RenderGraph* graph, uint32_t frameIdx, uint32_t passIdx);
		
		VkImageView GetImageAttachment(RenderPassAttachment attachment, uint32_t frameIdx = 0xffffffff);
		BufferView GetBufferAttachment(RenderPassAttachment attachment, uint32_t frameIdx = 0xffffffff);

		bool		CheckAttachmentDirtyFlag(RenderPassAttachment attachment);
	
	private:
		RenderGraph* m_Graph;
		uint32_t     m_FrameIdx;
		uint32_t	 m_passIdx;
		uint32_t	 m_mergedPassIdx;
	};


	class RenderPass
	{
		friend class RenderPassRuntimeContext;
	public:
		RenderPass(RenderGraph* graph, const char* name, RenderPassType type, RenderPassExtension expectedExtension);

		opt<RenderPassAttachment> AddImageColorOutput(const char* name, ImageSlice range, VkImageViewType    viewType = VK_IMAGE_VIEW_TYPE_2D);
		opt<RenderPassAttachment> AddImageDepthOutput(const char* name, ImageSlice range, VkImageViewType    viewType = VK_IMAGE_VIEW_TYPE_2D);
		opt<RenderPassAttachment> AddImageDepthInput(const char* name, ImageSlice range, VkImageViewType	 viewType = VK_IMAGE_VIEW_TYPE_2D);
		opt<RenderPassAttachment> AddImageColorInput(const char* name, ImageSlice range, VkImageViewType	 viewType);
		
		opt<RenderPassAttachment> AddImageStorageInput(const char* name, ImageSlice range, VkImageViewType    viewType);
		opt<RenderPassAttachment> AddImageStorageOutput(const char* name, ImageSlice range, VkImageViewType    viewType);

		opt<RenderPassAttachment> AddBufferStorageInput(const char* name, BufferSlice range);
		opt<RenderPassAttachment> AddBufferStorageOutput(const char* name, BufferSlice range);
		opt<RenderPassAttachment> AddBufferInput(const char* name, BufferSlice range);

		void					  AttachInterface(ptr<RenderPassInterface> inter);
		
		ResourceInfo			  GetAttachmentInfo(const RenderPassAttachment& idx);
		void					  GetAttachmentOperationState(const RenderPassAttachment& idx, RenderPassAttachmentOperationState& state);
		VkImageLayout			  GetAttachmentExpectedState(const RenderPassAttachment& idx);
		bool					  RequireClearColor(const RenderPassAttachment& idx);
		void					  GetClearColor(const RenderPassAttachment& idx, VkClearValue& clearValue);
		void					  OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd);

		RenderPassExtension		  GetRenderPassExtension();

		bool					  ValidationCheck(std::string& msg);

		const char*				  GetName();

		const std::vector<ResourceHandle>& GetAttachedResourceHandles();
		const std::vector<RenderPassAttachment>& GetAttachments();

		RenderPassType GetType();

	private:
		std::string name;

		RenderGraph* m_Graph;
		ptr<RenderPassInterface> m_RenderPassInterface;

		std::vector<RenderPassAttachment> m_Attachments;
		std::vector<ResourceHandle>		  m_AttachmentResourceHandle;
		RenderPassType m_RenderPassType;
		
		RenderPassExtension m_ExpectedExtension;
	};

	class RenderPassInterface
	{
	public:
		RenderPassInterface(RenderPass* renderPass)
			:m_TargetPass(renderPass)
		{}

		// called when compiling graph
		virtual bool OnValidationCheck(std::string& msg) { return true; }

		virtual void GetClearValue(uint32_t attachment, VkClearValue& value) {}

		virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
			VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp) {}

		virtual void GetAttachmentExpectedViewType(uint32_t attachment, VkImageViewType& initialGuess)
		{}

		virtual void GetAttachmentExpectedState(uint32_t attachment, VkImageLayout& initialGuess) {}

		virtual void OnRender(RenderPassRuntimeContext& ctx, VkCommandBuffer cmd) = 0;

		virtual RenderPassType ExpectedType() = 0;

	protected:
		RenderPass* m_TargetPass;
	};

	template<typename RPIType,typename ...Args>
	void CreateRenderPassInterface(RenderPass* rp, Args... args)
	{
		static_assert(std::is_base_of_v<RenderPassInterface, RPIType>, "RPType should be derived from render pass interface");
		auto rpi = std::make_shared<RPIType>(rp, args...);
		rp->AttachInterface(rpi);
	}
}
