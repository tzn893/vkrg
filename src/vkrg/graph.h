#pragma once
#include "vkrg/common.h"
#include "vkrg/pass.h"
#include "vkrg/dag.h"

namespace vkrg
{
	enum class RenderGraphCompileState
	{
		Success,
		Error_RenderPassValidation,
		Error_CycleInGraph,
		Error_WriteAfterWrite,
		Error_FailToCreateRenderPass,
		Error_CompileTwice,
		Error_InvalidCompileOption
	};

	enum class RenderGraphRuntimeState
	{
		Success,
		Error_MissingExternalResourceAttachment
	};

	enum class RenderGraphRenderPassStyle
	{
		OneByOne,
		MergeGraphicsPasses
	};


	struct RenderGraphCompileOptions
	{
		RenderGraphCompileOptions()
		{
			flightFrameCount = 3;
			style = RenderGraphRenderPassStyle::OneByOne;
			addAutomaticTransferUsageFlag = false;
			disableFrameOnFlight = false;
			setDebugName = false;
			screenWidth = 0;
			screenHeight = 0;
		}

		uint32_t				   flightFrameCount = 3;
		RenderGraphRenderPassStyle style;
		// transfer src/dst usage flag will be added to all resources automatically
		// I think this is handy but I don't know what side effect it will cause
		bool					   addAutomaticTransferUsageFlag;

		uint32_t				   screenWidth, screenHeight;

		bool					   disableFrameOnFlight;
		bool					   setDebugName;
	};


	struct ResourceHandle
	{
		uint32_t	 idx;
		bool		 external;
	};

	struct LogicalResource
	{
		std::string    name;
		ResourceHandle handle;
		ResourceInfo   info;
		VkImageLayout  finalLayout;
	};
	

	struct RenderPassHandle
	{
		ptr<RenderPass> pass;
		uint32_t		idx;
	};
	
	struct RenderGraphDeviceContext
	{
		ptr<gvk::Context> ctx;
	};

	class RenderGraphScope 
	{
	public:
		RenderGraphScope(const char* name, RenderGraph* graph);
		RenderGraphScope(const RenderGraphScope& other);

		opt<ResourceHandle>	  FindGraphResource(const char* name);
		opt<ResourceHandle>   GetGraphResource(uint32_t idx);

		opt<RenderPassHandle> FindGraphRenderPass(const char* name);
		opt<RenderPassHandle> GetGraphRenderPass(uint32_t idx);

		opt<ResourceHandle>	  AddGraphResource(const char* name, ResourceInfo info, bool external, VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED);
		opt<RenderPassHandle> AddGraphRenderPass(const char* name, RenderPassType type);

		RenderGraphScope      Scope(const char* name);

	private:
		std::string GetScopeName(const char* name);

		std::string name;
		RenderGraph* graph;
	};
	

	class RenderGraphDataFrame
	{
		friend class RenderGraph;
	public:
		
		bool BindBuffer(const char* name, uint32_t frameIdx, ptr<gvk::Buffer> buffer);

		bool BindImage(const char* name, uint32_t frameIdx, ptr<gvk::Image> image);

	private:

		enum Target
		{
			Physical,
			External
		} m_Target;

		RenderGraphDataFrame(const RenderGraphDataFrame& df, Target target)
			:m_Target(target)
		{
			m_Graph = df.m_Graph;
			m_FlightFrameCount = df.m_FlightFrameCount;
		}

		RenderGraphDataFrame(RenderGraph* graph, Target target);

		RenderGraph* m_Graph;
		uint32_t	 m_FlightFrameCount;
	};

	struct RenderGraphBarrier
	{
		VkPipelineStageFlagBits srcStage;
		VkPipelineStageFlagBits dstStage;

		std::vector<VkImageMemoryBarrier> imageBarriers[4];
		std::vector<VkBufferMemoryBarrier> bufferBarriers[4];
		struct Handle
		{
			uint32_t idx;
			bool external;
		};
		std::vector<Handle>	imageBarrierHandles;
		std::vector<Handle> bufferBarrierHandles;
	};


	class RenderGraph
	{
		friend class RenderGraphDataFrame;
		friend class RenderPassRuntimeContext;
	public:
		RenderGraph();

		opt<ResourceHandle>	  FindGraphResource(const char* name);
		opt<ResourceHandle>   GetGraphResource(uint32_t idx);

		opt<RenderPassHandle> FindGraphRenderPass(const char* name);
		opt<RenderPassHandle> GetGraphRenderPass(uint32_t idx);

		opt<ResourceHandle>	  AddGraphResource(const char* name, ResourceInfo info, bool external, VkImageLayout expectedFinalLayout = VK_IMAGE_LAYOUT_UNDEFINED);
		opt<RenderPassHandle> AddGraphRenderPass(const char* name, RenderPassType type, RenderPassExtension expectedExt = RenderPassExtension());

		tpl<RenderGraphCompileState, std::string> Compile(RenderGraphCompileOptions options, RenderGraphDeviceContext ctx);

		RenderGraphScope	  Scope(const char* name);

		ResourceInfo		  GetResourceInfo(ResourceHandle handle);

		void				  OnResize(uint32_t width, uint32_t height);

		tpl<RenderGraphRuntimeState, std::string>	Execute(uint32_t targetFrameIdx, VkCommandBuffer mainCmdBuffer);
		tpl<gvk::ptr<gvk::RenderPass>, uint32_t>    GetCompiledRenderPassAndSubpass(RenderPassHandle handle);

		RenderGraphDataFrame  GetExternalDataFrame();

	private:
		static constexpr uint32_t invalidIdx = 0xffffffff;

		RenderGraphRuntimeState	ValidateResourceBinding(std::string& msg);
		
		RenderGraphCompileState ValidateCompileOptions(std::string& msg);
		RenderGraphCompileState ValidateRenderPasses(std::string& msg);
		RenderGraphCompileState CollectedResourceDependencies(std::string& msg);
		RenderGraphCompileState BuildGraph(std::string& msg);
		
		RenderGraphCompileState ScheduleMergedGraph(std::string& msg);
		RenderGraphCompileState ScheduleOneByOneGraph(std::string& msg);

		RenderGraphCompileState AssignPhysicalResources(std::string& msg);
		RenderGraphCompileState ResolveDependenciesAndCreateRenderPasses(std::string& msg);

		GvkImageCreateInfo		CreateImageCreateInfo(ResourceInfo info);

		void					ResizePhysicalResources();
		void					UpdateDirtyViews();
		void					UpdateDirtyFrameBuffersAndBarriers();
		void					ResetResourceBindingDirtyFlag();
		void					GenerateCommands(VkCommandBuffer cmd, uint32_t frameIdx);


		void					InitializeRPFrameBufferTable();
		void					InitializeRenderPassViewTable();
		void					ClearCompileCache();
		void					PostCompile();


		uint32_t				GetResourceFrameIdx(uint32_t idx,bool res);

		RenderGraphCompileOptions m_Options;

		std::unordered_map<std::string, uint32_t> m_LogicalResourceTable;
		std::vector<LogicalResource> m_LogicalResourceList;


		std::unordered_map<std::string, uint32_t> m_RenderPassTable;
		std::vector<RenderPassHandle> m_RenderPassList;

		using DAGNode = DirectionalGraph<RenderPassHandle>::NodeIterator;
		using DAGAdjNode = DirectionalGraph<RenderPassHandle>::NodeAdjucentIterator;

		std::vector<DAGNode> m_RenderPassNodeList;

		struct ResourceIO
		{
			std::vector<uint32_t> resourceWriteList;
			std::vector<uint32_t> resourceReadList;
		};
		std::vector<ResourceIO>	m_LogicalResourceIODenpendencies;

		DirectionalGraph<RenderPassHandle>	m_Graph;


		struct MergedRenderPass
		{
			uint32_t idx;
			std::vector<DAGNode> renderPasses;
			bool     canBeMerged;
			RenderPassExtension expectedExtension;
		};

		using DAGMergedNode = DirectionalGraph<MergedRenderPass>::NodeIterator;

		uint32_t			 ScoreMergedNode(DAGMergedNode node);

		DirectionalGraph<MergedRenderPass>		   m_MergedRenderPassGraph;
		// std::vector<DAGMergedNode>				   m_MergedRenderPasses;

		DAGMergedNode	   CreateNewMergedNode(DAGNode node, bool mergable);
		opt<tpl<DAGMergedNode, uint32_t>> FindInvolvedMergedPass(DAGNode node);

		// find the last node write/read the resource
		// this function should be used for external resources
		// because physical resource might be merged
		DAGMergedNode FindLastAccessedNodeForResource(uint32_t logicalResourceIdx);

		DAGMergedNode FindFirstAccessedNodeForResource(uint32_t logicalResourceIdx);

		tpl<uint32_t, uint32_t, uint32_t> GetExpectedExtension(ResourceInfo::Extension ext, ResourceExtensionType type);

		uint32_t GetRenderGraphPassInfoIndex(DAGMergedNode node);

		struct ResourceAssignment
		{
			ResourceAssignment()
			{
				idx = invalidIdx;
				external = false;
			}

			uint32_t idx ;
			bool	 external;

			bool Invalid()
			{
				return idx == invalidIdx;
			}
		};

		struct PhysicalResource
		{
			ResourceInfo info;
			std::vector<uint32_t> logicalResources;
		};

		struct ExternalResource
		{
			ResourceHandle handle;
			std::string	   name;
		};

		std::vector<ResourceAssignment>  m_LogicalResourceAssignmentTable;
		std::vector<PhysicalResource> m_PhysicalResources;
		std::vector<ExternalResource> m_ExternalResources;

		
		RenderGraphDeviceContext m_vulkanContext;
		struct RenderGraphPassInfo
		{
			RenderPassType type;
			//std::vector<uint32_t> attachmentIndices;
			
			// for compute passes
			struct Compute
			{
				std::vector<RenderGraphBarrier> barriers;
				uint32_t targetRenderPass;
			} compute;

			struct FBAttachment
			{
				VkImageViewType      viewType;
				ResourceAssignment assign;
				ImageSlice		   subresource;
			};
			
			// table records witch frame buffer does the logical resource attachmented to 
			// for rernder passes
			struct Render
			{
				ptr<gvk::RenderPass>  renderPass;
				std::vector<uint32_t> mergedSubpassIndices;

				std::vector<RenderGraphBarrier> bufferBarriers;

				std::vector<FBAttachment> fbAttachmentIdx;
				std::vector<VkClearValue> fbClearValues;
				
				RenderPassExtension expectedExtension;
			} render;

			uint32_t targetMergedPassIdx;

			bool IsGeneralPass();
			bool IsGraphicsPass();
		};
		std::vector<RenderGraphPassInfo> m_renderGraphPassInfo;
		std::vector<RenderGraphBarrier> m_finalGlobalBarriers;

		bool SubresourceCompability(RenderPassAttachment& lhs, RenderPassAttachment& rhs);
		bool ResourceCompability(ResourceInfo& lhs, ResourceInfo& rhs);
		bool CheckImageUsageCompability(VkFormat lhs, VkImageUsageFlags usages);
		bool CheckBufferUsageCompability(VkFormat lhs, VkBufferUsageFlags usages);
	
		// one render graph can only compile once
		bool m_HaveCompiled = false;

		struct ResourceFormatCompabilityCache
		{
			std::vector<bool>				initialized;
			std::vector<VkFormatProperties> formatProperties;
			static constexpr uint32_t       supportedFormatCount = VK_FORMAT_ASTC_12x12_SRGB_BLOCK + 1;
		} m_FormatCompabilityCache;

		static constexpr uint32_t maxFrameOnFlightCount = 4;

		struct RenderPassViewTable
		{
			struct View
			{
				union 
				{
					VkImageView imageView;
					BufferView bufferView;
				};
				bool isImage;
			};

			std::vector<View> attachmentViews[maxFrameOnFlightCount];
		};
		std::vector<RenderPassViewTable> m_RPViewTable;

		struct ResourceBindingInfo
		{
			ptr<gvk::Buffer> buffers[maxFrameOnFlightCount];
			ptr<gvk::Image>  images[maxFrameOnFlightCount];
			bool			 dirtyFlag;
		};
		std::vector<ResourceBindingInfo> m_ExternalResourceBindings;
		std::vector<ResourceBindingInfo> m_PhysicalResourceBindings;

		ResourceBindingInfo* GetAssignedResourceBinding(ResourceAssignment assign);

		// list of frame buffers 
		struct RPFrameBuffer
		{
			VkFramebuffer frameBuffer[maxFrameOnFlightCount];
			std::vector<VkImageView> frameBufferViews[maxFrameOnFlightCount];
		};
		std::vector<RPFrameBuffer>    m_RPFrameBuffers;

		static constexpr VkImageTiling m_DefaultImageTiling = VK_IMAGE_TILING_OPTIMAL;
};


}