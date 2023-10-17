#pragma once
#include "vkrg/common.h"

namespace vkrg
{
	enum class ResourceExtensionType
	{
		Screen,
		Fixed,
		Buffer
	};


	struct ResourceInfo
	{
		ResourceExtensionType extType;
		union Extension
		{
			struct
			{
				float x; float y;
			} screen;
			struct
			{
				uint32_t x; uint32_t y; uint32_t z;
			} fixed;
			struct
			{
				uint64_t size;
			} buffer;
		} ext;

		VkFormat format;
		uint32_t mipCount;
		uint32_t channelCount;
		VkFlags  usages;
		VkFlags  extraFlags;

		VkImageType expectedDimension;

		ResourceInfo()
		{
			format = VK_FORMAT_UNDEFINED;
			mipCount = 1;
			channelCount = 1;
			usages = 0;
			memset(&ext, 0, sizeof(ext));
			extType = ResourceExtensionType::Screen;
			ext.screen.x = 1;
			ext.screen.y = 1;
			extraFlags = 0;
			expectedDimension = VK_IMAGE_TYPE_MAX_ENUM;
		}

		bool IsBuffer() const 
		{
			return extType == ResourceExtensionType::Buffer;
		}

		bool IsImage() const 
		{
			return !IsBuffer();
		}
	};

	using ImageSlice = VkImageSubresourceRange;
	
	struct BufferSlice
	{
		uint64_t size;
		uint64_t offset;
	};

	VkImageViewCreateInfo info;
}