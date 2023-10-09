#include "vkrg/compiler.h"
#include "vkrg/pass.h"
#include "vkrg/graph.h"

vkrg::ExecutablePassPrototypeInfoCollector::ExecutablePassPrototypeInfoCollector(GraphPass* pass)
{
	info.prototype = pass->GetPrototypeName();
}

uint32_t vkrg::ExecutablePassPrototypeInfoCollector::OutputScaleByScreen(const char* name, VKRG_FORMAT format, uint32_t channel, float ratio, VkImageUsageFlags usage)
{
	ExecutablePassParameter param;
	RenderGraphImageExtent& ext = param.info.ext.image;

	param.channel_count = channel;
	ext.fit_to_screen = true;
	param.info.format = format;
	param.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE2D;
	param.name = name;
	ext.ext.screen_scale = ratio;
	param.acquireUsage = usage;

	info.outputs.push_back(param);
	return info.outputs.size() - 1;
}

uint32_t vkrg::ExecutablePassPrototypeInfoCollector::Output(const char* name,  VKRG_FORMAT format, uint32_t channel, uint32_t width, uint32_t height, uint32_t depth, VkImageUsageFlags usage)
{
	ExecutablePassParameter param;
	RenderGraphImageExtent& ext = param.info.ext.image;


	param.channel_count = channel;
	ext.fit_to_screen = false;
	param.info.format = format;
	param.name = name;
	ext.ext.width = width;
	ext.ext.height = height;
	ext.ext.depth = depth;
	param.info.layout = VKRG_LAYOUT::VKRG_LAYOUT_TEXTURE2D;

	param.acquireUsage = usage;

	info.outputs.push_back(param);
	return info.outputs.size() - 1;
}


uint32_t vkrg::ExecutablePassPrototypeInfoCollector::Input(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, VkImageUsageFlags usage)
{
	// input's extent will be ignored
	ExecutablePassParameter param_info{};

	param_info.channel_count = channel;
	param_info.info.format = format;
	param_info.info.layout = layout;
	param_info.name = name;

	param_info.acquireUsage = usage;

	info.inputs.push_back(param_info);
	return info.inputs.size() - 1;
}

void vkrg::ExecutablePassPrototypeInfoCollector::SetType(VKRG_RENDER_PASS_TYPE type)
{
	info.type = type;
}

std::string GetEdgeString(vkrg::RenderGraphEdge edge)
{
	return std::string("edge from ") + edge.source + std::string(".") + edge.output + " to " + edge.dest + "." + edge.output;
}

vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> vkrg::RenderGraphCompiler::Compile(vkrg::RenderGraph* graph)
{
	m_unsortedRenderPass = m_renderGraph->m_passes;

	ClearCompiledCaches();
	m_renderGraph = graph;
	CollectPrototypes();
	CollectResources();


	{
		auto [err_code, msg] = CollectRenderPasses();
		if (err_code != VKRG_SUCCESS)
		{
			ClearCompiledCaches();
			return std::make_tuple(err_code, msg);
		}
	}

	{
		auto [err_code, msg] = BuildDirectionalAcyclicGraph();
		if (err_code != VKRG_SUCCESS)
		{
			ClearCompiledCaches();
			return std::make_tuple(err_code, msg);
		}
	}
	
	{
		auto [err_code, msg] = AllocateResources();
		if (err_code != VKRG_SUCCESS)
		{
			ClearCompiledCaches();
			return std::make_tuple(err_code, msg);
		}
	}

	{
		auto [err_code, msg] = ResolveResourceDependencies();
		if (err_code != VKRG_SUCCESS)
		{
			ClearCompiledCaches();
			return std::make_tuple(err_code, msg);
		}
	}

	return std::make_tuple(VKRG_SUCCESS, std::nullopt);
}



bool CheckParameterCompatibility(vkrg::GraphPassParameter input, vkrg::GraphPassParameter output)
{
	if (input.format != output.format || input.layout != output.layout) return false;
	
	return ;
}

vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> vkrg::RenderGraphCompiler::AllocateResources()
{
	m_renderPassInputParameterAttachment.resize(m_sortedRenderPass.size());
	m_renderPassOutputParameterAttachment.resize(m_sortedRenderPass.size());

	std::vector<ResourceAllocation> allocated_resources;

	uint32_t pass_count = m_sortedRenderPass.size();
	Node n = m_dag.Begin();
	for (; n != m_dag.End(); n++)
	{
		uint32_t pass_idx = n.GetId();
		auto& in_edges = m_renderPassInEdges[pass_idx];
		auto& out_edges = m_renderPassOutEdges[pass_idx];

		ptr<GraphPass> pass = *n;

		// Serial execution
		if (pass->IsExecutablePass())
		{
			uint32_t pass_prototype_idx;
			{
				auto iter = m_prototypeNameTable.find((*n)->GetPrototypeName());
				vkrg_assert(iter != m_prototypeNameTable.end());
				pass_prototype_idx = iter->second;
			}

			auto& prototype = m_prototypes[pass_prototype_idx];

			std::vector<RenderPassParameterResoruceAttachment> in_parameter_attachment(prototype.inputs.size());
			std::vector<bool> in_parameter_attachment_assigned(prototype.inputs.size(), false);
			std::vector<RenderPassParameterResoruceAttachment> out_parameter_attachment(prototype.outputs.size());

			std::unordered_set<uint32_t> occupied_resources;
			for (auto& e : in_edges)
			{
				RenderPassParameterResoruceAttachment input_attachment = m_renderPassOutputParameterAttachment[e.ext_pass.GetId()][e.ext_pass_parameter_idx];
				GraphPassParameter ext_pass_parameter;
				VkFlags			   usage;

				ptr<GraphPass> ext_pass = *e.ext_pass;

				if (ext_pass->IsExecutablePass())
				{
					auto prototype = m_prototypes[m_prototypeNameTable[ext_pass->GetPrototypeName()]];
					ext_pass_parameter = prototype.outputs[e.ext_pass_parameter_idx].info;
					usage = prototype.outputs[e.ext_pass_parameter_idx].acquireUsage;
				}
				else if (ext_pass->IsResourcePass())
				{
					auto prototype = m_resourcePrototypes[m_resourcePrototypeNameTable[ext_pass->GetPrototypeName()]];
					ext_pass_parameter = prototype.outputs[e.ext_pass_parameter_idx].info;
				}

				// check parameter
				GraphPassParameter pass_parameter = m_prototypes[m_prototypeNameTable[pass->GetName()]].inputs[e.parameter_idx].info;

				if (!CheckParameterCompatibility(pass_parameter, ext_pass_parameter))
				{
					return std::make_tuple(VKRG_INCOMPATIBLE_PARAMETERS,
						gvk::string_format("pass %s has incompatiable input parameter %d with pass %s output parameter %d", pass->GetName(),
							e.parameter_idx, ext_pass->GetName(), e.ext_pass_parameter_idx));
				}

				if (in_parameter_attachment_assigned[e.parameter_idx])
				{
					return std::make_tuple(VKRG_DUPLICATED_PARAMETER_ASSIGNMENT,
						gvk::string_format("pass %s's input parameter %d has duplicated assignment", pass->GetName(),
							e.parameter_idx));
				}

				in_parameter_attachment_assigned[e.parameter_idx] = true;
				in_parameter_attachment[e.parameter_idx] = input_attachment;

				if (pass->IsExecutablePass())
				{
					allocated_resources[input_attachment.idx].usage |= usage;
				}

				if (!input_attachment.external_resource)
				{
					occupied_resources.insert(input_attachment.idx);
				}
			}

			for (uint32_t iop = 0; iop < prototype.outputs.size(); iop++)
			{
				auto& op = prototype.outputs[iop];
				// find a compatible resource in unoccupied resources
				GraphPassParameter output_parameter = op.info;

				bool compatible_resource_found = false;
				for (uint32_t i = 0; i < allocated_resources.size(); i++)
				{
					if (CheckParameterCompatibility(allocated_resources[i].parameter, output_parameter) && !occupied_resources.count(i))
					{
						compatible_resource_found = true;
						RenderPassParameterResoruceAttachment attachment{};
						attachment.external_resource = false;
						attachment.idx = i;

						out_parameter_attachment[iop] = attachment;
						allocated_resources[i].usage |= op.acquireUsage;

						occupied_resources.insert(i);

						break;
					}
				}

				//	allocate a new resource if no avaliable compatible resource
				if (compatible_resource_found)
				{
					ResourceAllocation alloc;
					alloc.parameter = output_parameter;
					alloc.usage = op.acquireUsage;

					allocated_resources.push_back(alloc);

					RenderPassParameterResoruceAttachment attachment{};
					attachment.external_resource = false;
					attachment.idx = allocated_resources.size() - 1;

					out_parameter_attachment[iop] = attachment;

					occupied_resources.insert(attachment.idx);
				}					
			}
			
				
			if (pass->IsExecutablePass())
			{
				// all input parameters must be assigned to a input attachment
				for (uint32_t i = 0; i < in_parameter_attachment_assigned.size(); i++)
				{
					if (!in_parameter_attachment_assigned[i])
					{
						return std::make_tuple(VKRG_DUPLICATED_PARAMETER_ASSIGNMENT,
							gvk::string_format("pass %s's input parameter %d is not assigned", pass->GetName(), i));
					}
				}
			}


			m_renderPassInputParameterAttachment[pass_idx] = in_parameter_attachment;
			m_renderPassOutputParameterAttachment[pass_idx] = out_parameter_attachment;

		}
		else if (pass->IsResourcePass())
		{


		}
		else
		{
			// this statement should not be reached
			vkrg_assert(false);
		}

	}
	m_allocations = allocated_resources;
}

void vkrg::RenderGraphCompiler::ClearCompiledCaches()
{
	m_renderGraph = nullptr;

	m_unsortedRenderPass.clear();
	m_renderPassInEdges.clear();
	m_renderPassOutEdges.clear();

	m_nodes.clear();
	m_dag.Clear();
	m_prototypeNameTable.clear();
	m_prototypes.clear();
	m_renderPassNameIdxTable.clear();
	m_sortedRenderPass.clear();
	m_allocations.clear();

	m_resources.clear();
	m_resourceNameTable.clear();

	m_resourcePrototypes.clear();
	m_resourcePrototypeNameTable.clear();
}

vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> vkrg::RenderGraphCompiler::ResolveResourceDependencies()
{

}

void vkrg::RenderGraphCompiler::CollectPrototypes()
{
	auto& passes = m_renderGraph->m_passes;
	for (auto pass : passes)
	{
		if (pass->GetType() == VKRG_RP_TYPE_RENDER_PASS || pass->GetType() == VKRG_RP_TYPE_COMPUTE_PASS)
		{
			ExecutablePassPrototypeInfoCollector collector(pass.get());
			dynamic_cast<ExecutablePass*>(pass.get())->GeneratePrototypeInfo(collector);

			if (!m_prototypeNameTable.count(pass->GetPrototypeName()))
			{
				uint32_t idx = m_prototypes.size();
				m_prototypes.push_back(collector.info);
				m_prototypeNameTable[pass->GetPrototypeName()] = idx;
			}
		}
	}

	for (auto res : m_renderGraph->m_resourcePrototypes)
	{
		// resource prototypes are named automatically
		// resource prototype with the same name should not occur
		vkrg_assert(!m_resourcePrototypeNameTable.count(res.prototype));
		m_resourcePrototypes.push_back(res);
		m_resourcePrototypeNameTable[res.prototype] = m_resourcePrototypes.size() - 1;
	}

}

vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> vkrg::RenderGraphCompiler::BuildDirectionalAcyclicGraph()
{
	m_renderPassInEdges.resize(m_renderGraph->m_passes.size());
	m_renderPassOutEdges.resize(m_renderGraph->m_passes.size());

	// collect edge infos
	for (auto& e : m_renderGraph->m_edges)
	{
		uint32_t src_pass_idx;
		if (auto iter = m_renderPassNameIdxTable.find(e.source); iter != m_renderPassNameIdxTable.end())
		{
			src_pass_idx = iter->second;
		}
		else
		{
			return std::make_tuple(VKRG_INVALID_EDGE_INVALID_RENDER_PASS_NAME, std::string("invalid render pass name ") + e.source +
				std::string(" : ") + GetEdgeString(e));
		}

		uint32_t dst_pass_idx;
		if (auto iter = m_renderPassNameIdxTable.find(e.dest); iter != m_renderPassNameIdxTable.end())
		{
			dst_pass_idx = iter->second;
		}
		else
		{
			return std::make_tuple(VKRG_INVALID_EDGE_INVALID_RENDER_PASS_NAME, std::string("invalid render pass name ") + e.dest +
				std::string(" : ") + GetEdgeString(e));
		}

		uint32_t src_pass_proto_idx;
		{
			auto iter = m_prototypeNameTable.find(m_renderGraph->m_passes[src_pass_idx]->GetName());
			vkrg_assert(iter != m_prototypeNameTable.end());
			src_pass_proto_idx = iter->second;
		}

		uint32_t dst_pass_proto_idx;
		{
			auto iter = m_prototypeNameTable.find(m_renderGraph->m_passes[dst_pass_idx]->GetName());
			vkrg_assert(iter != m_prototypeNameTable.end());
			dst_pass_proto_idx = iter->second;
		}

		auto src_proto = m_prototypes[src_pass_proto_idx];
		auto dst_proto = m_prototypes[dst_pass_proto_idx];

		auto src_find_cmp = [&](const ExecutablePassParameter& v) {return v.name == e.output; };
		auto dst_find_cmp = [&](const ExecutablePassParameter& v) {return v.name == e.input; };


		uint32_t src_param_idx;
		if (auto iter = std::find_if(src_proto.outputs.begin(), src_proto.outputs.end(), src_find_cmp);
			iter != src_proto.outputs.end())
		{
			src_param_idx = iter - src_proto.outputs.begin();
		}
		else
		{
			return std::make_tuple(VKRG_INVALID_EDGE_INVALID_PARAMETER_NAME, std::string("invalid parameter name ") + e.output +
				std::string(" : ") + GetEdgeString(e));
		}

		uint32_t dst_param_idx;
		if (auto iter = std::find_if(dst_proto.inputs.begin(), dst_proto.inputs.end(), dst_find_cmp);
			iter != dst_proto.inputs.end())
		{
			dst_param_idx = iter - dst_proto.inputs.begin();
		}
		else
		{
			return std::make_tuple(VKRG_INVALID_EDGE_INVALID_PARAMETER_NAME, std::string("invalid parameter name ") + e.input +
				std::string(" : ") + GetEdgeString(e));
		}

		m_dag.AddEdge(m_nodes[src_pass_idx], m_nodes[dst_pass_idx]);

		CompiledRenderGraphEdge edge;
		edge.ext_pass = m_nodes[dst_pass_idx];
		edge.ext_pass_parameter_idx = dst_param_idx;
		edge.parameter_idx = src_param_idx;
		m_renderPassOutEdges[src_pass_idx].push_back(edge);
		
		edge.ext_pass = m_nodes[src_pass_idx];
		edge.ext_pass_parameter_idx = src_param_idx;
		edge.parameter_idx = dst_param_idx;
		m_renderPassInEdges[dst_pass_idx].push_back(edge);
	}
	
	{
		if (!m_dag.SortByPriority([](const ptr<GraphPass>& lhs, const ptr<GraphPass>& rhs) { return lhs->GetType() < rhs->GetType(); }))
		{
			return std::make_tuple(VKRG_CYCLE_IN_GRAPH, std::string("fail to sort graph: find circle in graph"));
		}
	}

	for (auto iter = m_dag.Begin(); iter != m_dag.End(); iter++)
	{
		m_sortedRenderPass.push_back(m_renderGraph->m_passes[iter.GetId()]);
	}
	vkrg_assert(m_sortedRenderPass.size() == m_renderGraph->m_passes.size());

	return std::make_tuple(VKRG_SUCCESS, std::nullopt);
}

vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> vkrg::RenderGraphCompiler::CollectRenderPasses()
{
	for (auto& pass : m_unsortedRenderPass)
	{
		
		if (m_renderPassNameIdxTable.count(pass->GetName()))
		{
			return std::make_tuple(VKRG_DUPLICATED_RENDER_PASS_NAME, std::string("duplicated render pass name ") + pass->GetName());
		}
		if (pass->GetType() == VKRG_RP_TYPE_RESOURCE_INPUT || pass->GetType() == VKRG_RP_TYPE_RESOURCE_IN_OUT || pass->GetType() == VKRG_RP_TYPE_RESOURCE_OUTPUT)
		{
			vkrg_assert(m_resourcePrototypeNameTable.count(pass->GetPrototypeName()));
		}
		m_renderPassNameIdxTable[pass->GetName()] = m_nodes.size();

		m_nodes.push_back(m_dag.AddNode(pass));
	}

	return std::make_tuple(VKRG_SUCCESS, std::nullopt);
}

vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> vkrg::RenderGraphCompiler::CollectResources()
{
	m_resources = m_renderGraph->m_resources;
	m_resourceNameTable = m_renderGraph->m_resourceNameTable;
}



