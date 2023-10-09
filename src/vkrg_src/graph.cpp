#include "vkrg/graph.h"
#include "vkrg/prototypes.h"
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
	// back buffer and depth buffer resource will be automatically added to render graph when created
	{
		RenderGraphResource res;
		res.array_count = 1;
		res.format = VKRG_FORMAT::VKRG_FORMAT_RGBA8;
		res.layout = VKRG_RESOURCE_LAYOUT::VKRG_RESOURCE_LAYOUT_TEXTURE2D;
		res.mip_count = 1;
		res.name = "back-buffer";

		auto [succ, _] = AddResource(res);
		// this operation should not fail
		vkrg_assert(succ);
	}
	
	{
		RenderGraphResource res;
		res.array_count = 1;
		res.format = VKRG_FORMAT::VKRG_FORMAT_D24S8;
		res.layout = VKRG_RESOURCE_LAYOUT::VKRG_RESOURCE_LAYOUT_TEXTURE2D;
		res.mip_count = 1;
		res.name = "depth-buffer";

		auto [succ, _] = AddResource(res);
		// this operation should not fail
		vkrg_assert(succ);
	}
}

void vkrg::RenderGraph::StoreToJson(const std::string& path)
{

}


vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> RenderGraph::Compile()
{
	return m_compiler.Compile(this);
}


void RenderGraph::Clear()
{
	m_resources.clear();
	m_resourceNameTable.clear();

	/*m_resourcePasses.clear();
	m_resourcePassNameSet.clear();
	m_resourceEdges.clear();
	m_resourceNameTable.clear();*/


	m_passes.clear();
	m_edges.clear();
	m_compiler.ClearCompiledCaches();
}

tpl<bool, opt<std::string>> RenderGraph::AddResource(RenderGraphResource resource)
{
	if (m_resourceNameTable.count(resource.name))
	{
		return std::make_tuple(false, std::string("repeated resource name: ") + resource.name);
	}
	m_resources.push_back(resource);
	m_resourceNameTable[resource.name] = m_resources.size() - 1;
	return std::make_tuple(true, std::nullopt);
}

struct FindRenderPassPrototypeCmp
{
	FindRenderPassPrototypeCmp(const vkrg::ResourcePassPrototypeInfo& prototype)
	{
		this->prototype = prototype;
	}

	bool operator()(const vkrg::ResourcePassPrototypeInfo& other)
	{
		if (prototype.type != other.type) return false;
		if (prototype.resource != other.resource) return false;
		if (prototype.outputs.size() != other.outputs.size()) return false;
		if (prototype.inputs.size() != other.inputs.size()) return false;
		for (uint32_t i = 0;i < prototype.outputs.size();i++)
		{
			const auto& param0 = prototype.outputs[i];
			const auto& param1 = other.outputs[i];

			if (param1.slice.arr_cnt != param0.slice.arr_cnt ||
				param1.slice.arr_idx != param0.slice.arr_idx ||
				param1.slice.mip_cnt != param0.slice.mip_cnt ||
				param1.slice.mip_idx != param0.slice.mip_idx)
			{
				return false;
			}
		}
		return true;
	}

	vkrg::ResourcePassPrototypeInfo prototype;
};


tpl<bool, opt<std::string>> vkrg::RenderGraph::AddResourceInput(const char* src_resource, const char* dst_pass, const char* dst_input, ResourceSlice slice)
{
	ResourcePassPrototypeInfo prototype;
	prototype.resource = src_resource;

	ResourcePassParameter parameter{};
	if (auto iter = m_resourceNameTable.find(prototype.resource); iter != m_resourceNameTable.end())
	{
		RenderGraphResource& resource = m_resources[iter->second];
		parameter.info.format = resource.format;
		switch (resource.layout)
		{
		case VKRG_RESOURCE_LAYOUT_BUFFER:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_BUFFER;
			break;
		case VKRG_RESOURCE_LAYOUT_TEXTURE2D:
		case VKRG_RESOURCE_LAYOUT_TEXTURE2D_ARRAY:
		case VKRG_RESOURCE_LAYOUT_TEXTURE2D_CUBE:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE2D;
			break;
		case VKRG_RESOURCE_LAYOUT_TEXTURE3D:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE3D;
			break;
		case VKRG_RESOURCE_LAYOUT_TEXTURE1D:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE1D;
			break;
		default:
			// this case shouldn't be reached
			vkrg_assert(false);
		}

		parameter.slice = slice;
	}
	else
	{
		return std::make_tuple(false, std::string("invalid resource name : ") + prototype.resource);
	}
	parameter.name = src_resource;
	prototype.inputs.push_back(parameter);
	prototype.type = VKRG_RENDER_PASS_TYPE::VKRG_RP_TYPE_RESOURCE_INPUT;

	std::string prototype_name;

	// operation shouldn't fail from now on
	// find prototype in prototype lists
	if (auto iter = std::find_if(m_resourcePrototypes.begin(), m_resourcePrototypes.end(), FindRenderPassPrototypeCmp(prototype));
		iter != m_resourcePrototypes.end())
	{
		prototype_name = iter->prototype;
	}
	else
	{
		std::string prototype_name_prefix = src_resource + std::string("-input_");
		uint32_t idx = 0;
		prototype_name = prototype_name_prefix + std::to_string(idx);
		while (m_resourcePrototypeNameSet.count(prototype_name))
		{
			prototype_name = prototype_name_prefix + std::to_string(++idx);
		}
		prototype.prototype = prototype_name;
		m_resourcePrototypeNameSet.insert(prototype_name);
		m_resourcePrototypes.push_back(prototype);
	}

	std::string pass_name;
	{
		std::string pass_name_prefix = prototype_name + "-";
		uint32_t idx = 0;
		pass_name = pass_name_prefix + std::to_string(idx);

		while (m_resourcePassNameSet.count(pass_name))
		{
			pass_name = pass_name_prefix + std::to_string(++idx);
		}
	}

	// we wouldn't validate edge here, this will be checked at compile time
	ptr<GraphPass> pass = std::make_shared<ResourcePass>(pass_name, slice, prototype);
	m_resourcePassNameSet.insert(pass_name);

	RenderGraphEdge edge;
	edge.source = pass_name;
	edge.output = prototype.inputs[0].name;
	edge.dest = dst_pass;
	edge.input = dst_input;

	m_edges.push_back(edge);
	m_passes.push_back(pass);

	return std::make_tuple(true, std::nullopt);
}

tpl<bool, opt<std::string>> vkrg::RenderGraph::AddResourceOutput(const char* src_pass, const char* src_output, const char* dst_resource, ResourceSlice slice)
{
	ResourcePassPrototypeInfo prototype;
	prototype.resource = dst_resource;

	ResourcePassParameter parameter{};
	if (auto iter = m_resourceNameTable.find(prototype.resource); iter != m_resourceNameTable.end())
	{
		RenderGraphResource& resource = m_resources[iter->second];
		parameter.info.format = resource.format;
		switch (resource.layout)
		{
		case VKRG_RESOURCE_LAYOUT_BUFFER:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_BUFFER;
			break;
		case VKRG_RESOURCE_LAYOUT_TEXTURE2D:
		case VKRG_RESOURCE_LAYOUT_TEXTURE2D_ARRAY:
		case VKRG_RESOURCE_LAYOUT_TEXTURE2D_CUBE:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE2D;
			break;
		case VKRG_RESOURCE_LAYOUT_TEXTURE3D:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE3D;
			break;
		case VKRG_RESOURCE_LAYOUT_TEXTURE1D:
			parameter.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE1D;
			break;
		default:
			// this case shouldn't be reached
			vkrg_assert(false);
		}

		parameter.slice = slice;
	}
	else
	{
		return std::make_tuple(false, std::string("invalid resource name : ") + prototype.resource);
	}
	parameter.name = dst_resource;
	prototype.inputs.push_back(parameter);
	prototype.type = VKRG_RENDER_PASS_TYPE::VKRG_RP_TYPE_RESOURCE_OUTPUT;

	std::string prototype_name;

	// operation shouldn't fail from now on
	// find prototype in prototype lists
	if (auto iter = std::find_if(m_resourcePrototypes.begin(), m_resourcePrototypes.end(), FindRenderPassPrototypeCmp(prototype));
		iter != m_resourcePrototypes.end())
	{
		prototype_name = iter->prototype;
	}
	else
	{
		std::string prototype_name_prefix = dst_resource + std::string("-output");
		uint32_t idx = 0;
		prototype_name = prototype_name_prefix + std::to_string(idx);
		while (m_resourcePrototypeNameSet.count(prototype_name))
		{
			prototype_name = prototype_name_prefix + std::to_string(++idx);
		}
		prototype.prototype = prototype_name;
		m_resourcePrototypeNameSet.insert(prototype_name);
		m_resourcePrototypes.push_back(prototype);
	}

	std::string pass_name;
	{
		std::string pass_name_prefix = prototype_name + "-";
		uint32_t idx = 0;
		pass_name = pass_name_prefix + std::to_string(idx);

		while (m_resourcePassNameSet.count(pass_name))
		{
			pass_name = pass_name_prefix + std::to_string(++idx);
		}
	}

	// we wouldn't validate edge here, this will be checked at compile time
	ptr<GraphPass> pass = std::make_shared<ResourcePass>(pass_name, slice, prototype);
	m_resourcePassNameSet.insert(pass_name);

	RenderGraphEdge edge;
	edge.source = src_pass;
	edge.output = src_output;
	edge.dest = pass_name;
	edge.input = prototype.inputs[0].name;

	m_edges.push_back(edge);
	m_passes.push_back(pass);

	return std::make_tuple(true, std::nullopt);
}

void RenderGraph::AddEdge(const char* src_pass, const char* src_output, const char* dst_pass, const char* dst_input)
{
	m_edges.push_back(RenderGraphEdge{ src_pass, src_output, dst_pass, dst_input });
}

void RenderGraph::AddPass(const ptr<GraphPass>& pass)
{
	m_passes.push_back(pass);
}

