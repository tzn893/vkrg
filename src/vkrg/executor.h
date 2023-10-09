#pragma once
#include "vkrg/common.h"

enum VKRG_EXECUTOR_STYLE
{
	VKRG_EXECUTOR_SINGLE_SUBPASSES,
	// VKRG_EXECUTOR_MULTI_SUBPASSES,
	// VKRG_EXECUTOR_ASYNC_SINGLE_SUBPASSES,
	// VKRG_EXECUTOR_ASYNC_MULTI_SUBPASSES
};

namespace vkrg
{
	class RenderGraph;

	class RenderGraphExecutor
	{
	public:
		bool LoadGraph(RenderGraph* graph, VKRG_EXECUTOR_STYLE style);

		void Execute(VkCommandBuffer cmd);
	private:
		RenderGraph* m_graph;
	};
}