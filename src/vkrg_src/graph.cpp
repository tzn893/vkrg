#include "vkrg/graph.h"
using namespace vkrg;


opt<vkrg::ptr<vkrg::RenderGraph>> vkrg::RenderGraph::LoadFromJson(const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open())
	{
		return std::nullopt;
	}
	return LoadFromJson(file);
}

vkrg::opt<vkrg::ptr<vkrg::RenderGraph>> RenderGraph::LoadFromJson(const char* data)
{
	return std::nullopt;
}

vkrg::opt<vkrg::ptr<vkrg::RenderGraph>> RenderGraph::LoadFromJson(std::ifstream& stream)
{
	stream.seekg(0, std::ios_base::end);
	size_t size = stream.tellg();
	stream.seekg(0, std::ios_base::beg);

	char* data = (char*)malloc(size);
	stream.read(data, size);

	auto rv = LoadFromJson(data);
	
	free(data);
	return rv;
}

vkrg::RenderGraph::RenderGraph()
{

}

void vkrg::RenderGraph::StoreToJson(const std::string& path)
{

}


vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> RenderGraph::Compile()
{
	return m_compiler.Compile(this);
}

void vkrg::RenderGraph::Execute()
{

}

void RenderGraph::AddEdge(const char* src_pass, const char* src_output, const char* dst_pass, const char* dst_input)
{
	m_edges.push_back(RenderGraphEdge{ src_pass, src_output, dst_pass, dst_input });
}

void RenderGraph::AddPass(const ptr<RenderPass>& pass)
{
	m_renderPasses.push_back(pass);
}

