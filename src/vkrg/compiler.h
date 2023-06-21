#pragma once
#include "vkrg/common.h"

enum VKRG_RENDER_PASS_TYPE
{
	VKRG_RP_TYPE_INVALID,
	VKRG_RP_TYPE_RENDER_PASS,
	VKRG_RP_TYPE_COMPUTE_PASS,
	VKRG_RP_TYPE_RESOURCE_OUTPUT,
	VKRG_RP_TYPE_RESOURCE_INPUT,
	VKRG_RP_TYPE_RESOURCE_IN_OUT
};

enum VKRG_RESOURCE_LAYOUT
{
	VKRG_RESOURCE_LAYOUT_TEXTURE1D,
	VKRG_RESOURCE_LAYOUT_TEXTURE2D,
	VKRG_RESOURCE_LAYOUT_TEXTURE2D_ARRAY,
	VKRG_RESOURCE_LAYOUT_TEXTURE2D_CUBE,
	VKRG_RESOURCE_LAYOUT_TEXTURE3D,
	VKRG_RESOURCE_LAYOUT_BUFFER
};

enum VKRG_LAYOUT
{
	VKRG_LAYOUT_TEXTURE1D,
	VKRG_LAYOUT_TEXTURE2D,
	VKRG_LAYOUT_TEXTURE3D,
	VKRG_LAYOUT_BUFFER
};

enum VKRG_FORMAT
{
	VKRG_FORMAT_BUFFER,
	VKRG_FORMAT_RGBA8,
	VKRG_FORMAT_D24S8
};


enum VKRG_COMPILE_STATE
{
	VKRG_SUCCESS,
	VKRG_DUPLICATED_RENDER_PASS_NAME,
	VKRG_DUPLICATED_RESOURCE_NAME,
	VKRG_DUPLICATED_PARAMETER_NAME,
	VKRG_INVALID_EDGE_INVALID_RENDER_PASS_NAME,
	VKRG_INVALID_EDGE_INVALID_PARAMETER_NAME,
	VKRG_CYCLE_IN_GRAPH
};

namespace vkrg
{
	struct RenderPassPropotypeParameterInfo
	{
		std::string name;
		VKRG_LAYOUT layout;
		VKRG_FORMAT format;
		union Extent
		{
			struct 
			{
				uint32_t width;
				uint32_t height;
				uint32_t depth;
			};
			float screen_scale;
		} extent;
		bool fit_to_screen;
		uint32_t channel_count;
	};


	struct RenderPassPrototypeInfo
	{
		std::string prototype;
		VKRG_RENDER_PASS_TYPE type;
		std::vector<RenderPassPropotypeParameterInfo> inputs;
		std::vector<RenderPassPropotypeParameterInfo> outputs;
	};


	class RenderPass;

	class RenderPassPrototypeInfoCollector
	{
		friend class RenderGraphCompiler;
		friend class RenderGraph;
	public:
		RenderPassPrototypeInfoCollector(RenderPass* pass);

		void SetType(VKRG_RENDER_PASS_TYPE type);

		uint32_t Input(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, uint32_t width, uint32_t height, uint32_t depth);
		uint32_t InputScaleByScreen(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, float ratio);

		uint32_t Output(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, uint32_t width, uint32_t height, uint32_t depth);
		uint32_t OutputScaleByScreen(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, float ratio);

	private:
		RenderPassPrototypeInfo info;
	};

	class RenderGraph;

	class RenderGraphCompiler
	{
		friend class vkrg::RenderGraph;
	public:
		tpl<VKRG_COMPILE_STATE, opt<std::string>> Compile(RenderGraph* graph);

	private:
		std::vector<RenderPassPrototypeInfo> m_prototypes;
		std::unordered_map<std::string, uint32_t> m_prototypeNameTable;
		

		std::vector<ptr<RenderPass>> m_sortedRenderPass;

	};
}

