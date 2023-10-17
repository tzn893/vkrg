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
			ImageColorInput,

			ImageStorageInput,
			ImageStorageOutput,

			BufferStorageInput,
			BufferStorageOutput,
			BufferInput
		} type;

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

	class RenderPass
	{
	public:
		RenderPass(RenderGraph* graph, const char* name, RenderPassType type);

		opt<RenderPassAttachment> AddImageColorOutput(const char* name, ImageSlice range);
		opt<RenderPassAttachment> AddImageDepthOutput(const char* name, ImageSlice range);
		opt<RenderPassAttachment> AddImageColorInput(const char* name, ImageSlice range);
		
		opt<RenderPassAttachment> AddImageStorageInput(const char* name, ImageSlice range);
		opt<RenderPassAttachment> AddImageStorageOutput(const char* name, ImageSlice range);

		opt<RenderPassAttachment> AddBufferStorageInput(const char* name, BufferSlice range);
		opt<RenderPassAttachment> AddBufferStorageOutput(const char* name, BufferSlice range);
		opt<RenderPassAttachment> AddBufferInput(const char* name, BufferSlice range);

		void					  AttachInterface(ptr<RenderPassInterface> inter);
		
		ResourceInfo			  GetAttachmentInfo(const RenderPassAttachment& idx);
		void					  GetAttachmentOperationState(const RenderPassAttachment& idx, RenderPassAttachmentOperationState& state);
		VkImageLayout			  GetAttachmentExpectedState(const RenderPassAttachment& idx);
		bool					  RequireClearColor(const RenderPassAttachment& idx);
		void					  GetClearColor(const RenderPassAttachment& idx, VkClearValue& clearValue);

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
	};

	class RenderPassInterface
	{
	public:
		RenderPassInterface(RenderPass* renderPass)
			:m_TargetPass(renderPass)
		{}

		// called when compiling graph
		virtual bool OnValidationCheck(std::string& msg) { return true; }

		virtual void GetClearValue(uint32_t attachment, VkClearValue& value) = 0;

		virtual void GetAttachmentStoreLoadOperation(uint32_t attachment, VkAttachmentLoadOp& loadOp, VkAttachmentStoreOp& storeOp,
			VkAttachmentLoadOp& stencilLoadOp, VkAttachmentStoreOp& stencilStoreOp) {}

		virtual VkImageLayout GetAttachmentExpectedState(uint32_t attachment) = 0;

		virtual void OnRender() = 0;

		virtual RenderPassType ExpectedType() = 0;

	protected:
		RenderPass* m_TargetPass;
	};

	template<typename RPIType,typename ...Args>
	void CreateRenderPassInterface(RenderPass* rp, Args... args)
	{
		static_assert(std::is_base_of_v<RenderPassInterface, RPType>, "RPType should be derived from render pass interface");
		auto rpi = std::make_shared<RPType>(rp, args...);
		rp->AttachInterface(rpi);
	}
}
