#include "vkrg/compiler.h"
#include "vkrg/pass.h"
#include "vkrg/graph.h"

vkrg::RenderPassPrototypeInfoCollector::RenderPassPrototypeInfoCollector(RenderPass* pass)
{
	info.prototype = pass->GetPrototypeName();
}

uint32_t vkrg::RenderPassPrototypeInfoCollector::OutputScaleByScreen(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, float ratio)
{
	RenderPassPropotypeParameterInfo param_info;
	param_info.channel_count = channel;
	param_info.fit_to_screen = true;
	param_info.format = format;
	param_info.layout = layout;
	param_info.name = name;
	param_info.extent.screen_scale = ratio;

	info.outputs.push_back(param_info);
	return info.outputs.size() - 1;
}

uint32_t vkrg::RenderPassPrototypeInfoCollector::Output(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, uint32_t width, uint32_t height, uint32_t depth)
{
	RenderPassPropotypeParameterInfo param_info;
	param_info.channel_count = channel;
	param_info.fit_to_screen = false;
	param_info.format = format;
	param_info.layout = layout;
	param_info.name = name;
	param_info.extent.width = width;
	param_info.extent.height = height;
	param_info.extent.depth = depth;

	info.outputs.push_back(param_info);
	return info.outputs.size() - 1;
}

uint32_t vkrg::RenderPassPrototypeInfoCollector::InputScaleByScreen(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, float ratio)
{
	RenderPassPropotypeParameterInfo param_info;
	param_info.channel_count = channel;
	param_info.fit_to_screen = true;
	param_info.format = format;
	param_info.layout = layout;
	param_info.name = name;
	param_info.extent.screen_scale = ratio;

	info.inputs.push_back(param_info);

	return info.inputs.size() - 1;
}

uint32_t vkrg::RenderPassPrototypeInfoCollector::Input(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, uint32_t width, uint32_t height, uint32_t depth)
{
	RenderPassPropotypeParameterInfo param_info;
	param_info.channel_count = channel;
	param_info.fit_to_screen = false;
	param_info.format = format;
	param_info.layout = layout;
	param_info.name = name;
	param_info.extent.width = width;
	param_info.extent.height = height;
	param_info.extent.depth = depth;

	info.inputs.push_back(param_info);
	return info.inputs.size() - 1;
}

void vkrg::RenderPassPrototypeInfoCollector::SetType(VKRG_RENDER_PASS_TYPE type)
{
	info.type = type;
}

vkrg::tpl<VKRG_COMPILE_STATE, vkrg::opt<std::string>> vkrg::RenderGraphCompiler::Compile(vkrg::RenderGraph* graph)
{
	auto& passes = graph->m_renderPasses;
	for (auto pass : passes)
	{
		RenderPassPrototypeInfoCollector collector(pass.get());
		pass->Compile(collector);

		if (!m_prototypeNameTable.count(pass->GetPrototypeName()))
		{
			uint32_t idx = m_prototypes.size();
			m_prototypes.push_back(collector.info);
			m_prototypeNameTable[pass->GetPrototypeName()] = idx;
		}
	}

	return make_tuple(VKRG_SUCCESS, std::nullopt);
}
