#pragma once
#include "vkrg/common.h"
#include "vkrg/prototypes.h"
#include "vkrg/compiler.h"
#include "vkrg/pass.h"
#include <unordered_map>
#include <unordered_set>

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
		friend class RenderGraphExecutor;
	public:
		static opt<ptr<RenderGraph>> LoadFromJson(const std::string& path);
		static opt<ptr<RenderGraph>> LoadFromJson(std::ifstream& stream);
		static opt<ptr<RenderGraph>> LoadFromJson(const char* data);

		RenderGraph();

		void StoreToJson(const std::string& path);

		tpl<VKRG_COMPILE_STATE, opt<std::string>> Compile();

		void AddEdge(const char* src_pass, const char* src_output, const char* dst_pass, const char* dst_input);

		tpl<bool, opt<std::string>> AddResourceInput(const char* src_resource, const char* dst_pass, const char* dst_input, ResourceSlice slice);

		tpl<bool, opt<std::string>> AddResourceOutput(const char* src_pass, const char* src_output, const char* dst_resource, ResourceSlice slice);

		void AddPass(const ptr<GraphPass>& pass);

		tpl<bool, opt<std::string>> AddResource(RenderGraphResource resource);

		template<typename T>
		void CreateRenderPass(const char* name)
		{
			auto pass = ExecutablePassPassFactory::CreateGraphPass(ExecutablePassReflectionPrototypeName<T>::name);
			AddPass(pass);
		}

		void Clear();

	private:
		std::vector<RenderGraphResource>	 m_resources;
		std::unordered_map <std::string, uint32_t> m_resourceNameTable;
		std::vector<ResourcePassPrototypeInfo> m_resourcePrototypes;
		std::unordered_set<std::string>		   m_resourcePrototypeNameSet;
		std::unordered_set<std::string>		   m_resourcePassNameSet;
		
		std::vector<ptr<GraphPass>> m_passes;
		std::vector<RenderGraphEdge> m_edges;
		RenderGraphCompiler m_compiler;
	};

}
