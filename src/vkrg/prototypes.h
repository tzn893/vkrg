#pragma once
#include "vkrg/common.h"

enum VKRG_RENDER_PASS_TYPE
{
	VKRG_RP_TYPE_INVALID,
	VKRG_RP_TYPE_COMPUTE_PASS,
	VKRG_RP_TYPE_RENDER_PASS,
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

namespace vkrg
{
	struct RenderGraphImageExtent
	{
		union Extent {
			struct
			{
				uint32_t width;
				uint32_t height;
				uint32_t depth;
			};
			float screen_scale;
		} ext;
		bool fit_to_screen;

		bool operator==(const RenderGraphImageExtent& ext);
	};


	struct GraphPassParameter
	{
		VKRG_LAYOUT layout;
		union Extent
		{
			RenderGraphImageExtent image;
			struct {
				uint32_t size;
			} buffer;
		} ext;

		VKRG_FORMAT format;
	};

	struct ExecutablePassParameter
	{
		std::string name;

		GraphPassParameter info;

		uint32_t channel_count;
		VkFlags	 acquireUsage;
	};

	struct ExecutablePassPrototypeInfo
	{
		std::string prototype;
		VKRG_RENDER_PASS_TYPE type;
		std::vector<ExecutablePassParameter> inputs;
		std::vector<ExecutablePassParameter> outputs;
	};

	class GraphPass;
	class ExecutablePass;
	class ResourcePass;

	class ExecutablePassPrototypeInfoCollector
	{
		friend class RenderGraphCompiler;
		friend class RenderGraph;
	public:
		ExecutablePassPrototypeInfoCollector(GraphPass* pass);

		void SetType(VKRG_RENDER_PASS_TYPE type);

		uint32_t Input(const char* name, VKRG_LAYOUT layout, VKRG_FORMAT format, uint32_t channel, VkImageUsageFlags usage);

		// for texture 2d
		uint32_t Output(const char* name, VKRG_FORMAT format, uint32_t channel, uint32_t width, uint32_t height, uint32_t depth, VkImageUsageFlags usage);
		// for texture 2d
		uint32_t OutputScaleByScreen(const char* name, VKRG_FORMAT format, uint32_t channel, float ratio, VkImageUsageFlags usage);

	private:
		ExecutablePassPrototypeInfo info;
	};

	// render graph resource
	struct RenderGraphResource
	{
		std::string name;
		VKRG_FORMAT format;
		uint32_t	mip_count;
		uint32_t	array_count;
		VKRG_RESOURCE_LAYOUT layout;
	};

	struct ResourceSlice
	{
		uint32_t mip_idx;
		uint32_t mip_cnt;
		uint32_t arr_idx;
		uint32_t arr_cnt;
	};

	struct ResourcePassParameter
	{
		std::string name;
		GraphPassParameter info;
		ResourceSlice slice;
	};

	struct ResourcePassPrototypeInfo
	{
		std::string prototype;
		VKRG_RENDER_PASS_TYPE type;
		std::vector<ResourcePassParameter> inputs;
		std::vector<ResourcePassParameter> outputs;
		std::string resource;
	};
}