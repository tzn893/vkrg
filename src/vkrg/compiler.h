#pragma once
#include "vkrg/common.h"
#include "vkrg/prototypes.h"
#include "vkrg/dag.h"

enum VKRG_COMPILE_STATE
{
	VKRG_SUCCESS,
	VKRG_DUPLICATED_RENDER_PASS_NAME,
	VKRG_DUPLICATED_RESOURCE_NAME,
	VKRG_DUPLICATED_PARAMETER_NAME,
	VKRG_INVALID_EDGE_INVALID_RENDER_PASS_NAME,
	VKRG_INVALID_EDGE_INVALID_PARAMETER_NAME,
	VKRG_CYCLE_IN_GRAPH,
	VKRG_INCOMPATIBLE_PARAMETERS,
	VKRG_DUPLICATED_PARAMETER_ASSIGNMENT
};

namespace vkrg
{
	class RenderGraph;

	class RenderGraphCompiler
	{
		friend class vkrg::RenderGraph;
	public:
		tpl<VKRG_COMPILE_STATE, opt<std::string>> Compile(RenderGraph* graph);

	private:

		vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>>	AllocateResources();
		void													ClearCompiledCaches();
		vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>>	ResolveResourceDependencies();
		void													CollectPrototypes();
		vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>>	BuildDirectionalAcyclicGraph();
		vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>>	CollectRenderPasses();
		vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>>	CollectResources();

		using Node = DirectionalGraph<ptr<GraphPass>>::NodeIterator;

		/*std::unordered_map<std::string, uint32_t> m_resourceNameTable;
		std::vector<RenderGraphResource> m_resources;
		struct ResourceSlice
		{
			ResourceSlice slice;
			uint32_t				 resource_idx;
		};
		std::vector<ResourceSlice> m_resourceSlices;*/

		std::vector<ExecutablePassPrototypeInfo> m_prototypes;
		std::unordered_map<std::string, uint32_t> m_prototypeNameTable;

		std::vector<ResourcePassPrototypeInfo> m_resourcePrototypes;
		std::unordered_map<std::string, uint32_t> m_resourcePrototypeNameTable;

		std::vector<RenderGraphResource>	 m_resources;
		std::unordered_map <std::string, uint32_t> m_resourceNameTable;

		struct RenderPassParameterResoruceAttachment
		{
			// when external_resoruce flag is set true idx is index of resource_pass_idx
			// when external_resource flag is set false idx is index of resource allocation
			uint32_t idx;
			bool	 external_resource;
		};

		// struct 
		std::unordered_map<std::string, uint32_t> m_renderPassNameIdxTable;
		struct  CompiledRenderGraphEdge
		{
			Node	 ext_pass;
			uint32_t parameter_idx;
			uint32_t ext_pass_parameter_idx;
		};

		// order follows unsorted render passes
		// you can access a pass's edges and attachments by its graph's node id
		std::vector<std::vector<CompiledRenderGraphEdge>> m_renderPassInEdges;
		std::vector<std::vector<CompiledRenderGraphEdge>> m_renderPassOutEdges;

		std::vector<std::vector<RenderPassParameterResoruceAttachment>> m_renderPassInputParameterAttachment;
		std::vector<std::vector<RenderPassParameterResoruceAttachment>> m_renderPassOutputParameterAttachment;

		// -------------------------------- //

		DirectionalGraph<ptr<GraphPass>> m_dag;
		std::vector<Node> m_nodes;
		
		std::vector<ptr<GraphPass>> m_sortedRenderPass;
		std::vector<ptr<GraphPass>> m_unsortedRenderPass;


		struct ResourceAllocation
		{
			GraphPassParameter parameter;
			// resource's usage will be decided at compile time 
			VkFlags	usage;
		};

		struct ExternalResource
		{
			GraphPassParameter parameter;
			ResourceSlice      slice;
		};

		std::vector<ResourceAllocation>    m_allocations;
		std::vector<ExternalResource>      m_externalResources;

		RenderGraph* m_renderGraph = nullptr;
	};
}

