#pragma once
#include "vkrg/common.h"
#include "vkrg/compiler.h"
#include "vkrg/pass.h"

namespace vkrg 
{
	struct RenderGraphEdge
	{
		std::string source;
		std::string output;
		std::string dest;
		std::string input;
	};

	class RenderGraphCompiler;

	class RenderGraph
	{
		friend class RenderGraphCompiler;
	public:
		static opt<ptr<RenderGraph>> LoadFromJson(const std::string& path);
		static opt<ptr<RenderGraph>> LoadFromJson(std::ifstream& stream);
		static opt<ptr<RenderGraph>> LoadFromJson(const char* data);

		RenderGraph();

		void StoreToJson(const std::string& path);

		tpl<VKRG_COMPILE_STATE, opt<std::string>> Compile();

		void Execute();

		void AddEdge(const char* src_pass, const char* src_output, const char* dst_pass, const char* dst_input);

		void AddPass(const ptr<RenderPass>& pass);

		template<typename T>
		void CreatePass(const char* name)
		{
			auto pass = RenderPassFactory::CreateRenderPass(RenderPassReflectionPrototypeName<T>::name);
			
		}

	private:
		std::vector<ptr<RenderPass>> m_renderPasses;
		std::vector<RenderGraphEdge> m_edges;
		RenderGraphCompiler m_compiler;
	};

}
