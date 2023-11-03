
/*
* Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE

#include "Model.h"
#include "vkrg/common.h"
#include "gvk.h"
#include <iostream>


/*
	We use a custom image loading function with tinyglTF, so we can do custom stuff loading ktx textures
*/
bool loadImageDataFunc(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData)
{
	// KTX files will be handled by our own code
	if (image->uri.find_last_of(".") != std::string::npos) {
		if (image->uri.substr(image->uri.find_last_of(".") + 1) == "ktx") {
			return true;
		}
	}

	return tinygltf::LoadImageData(image, imageIndex, error, warning, req_width, req_height, bytes, size, userData);
}

bool loadImageDataFuncEmpty(tinygltf::Image* image, const int imageIndex, std::string* error, std::string* warning, int req_width, int req_height, const unsigned char* bytes, int size, void* userData) 
{
	// This function will be used for samples that don't require images to be loaded
	return true;
}

static std::string strToLower(const char* str)
{
	std::string tmpStr = str;
	std::transform(tmpStr.begin(), tmpStr.end(), tmpStr.begin(), [&](char c) { return std::tolower(c); });
	return tmpStr;
}


SpvReflectDescriptorBinding* fliterDescriptorSetLayoutBinding(gvk::ptr<gvk::DescriptorSetLayout> layout,const std::vector<std::string>& keyWords)
{
	auto& bindings = layout->GetDescriptorSetBindings();
	
	for (uint32_t i = 0;i < bindings.size();i++)
	{
		std::string binding_name = bindings[i]->name;
		binding_name = strToLower(binding_name.c_str());
		for (auto kw : keyWords)
		{
			if (binding_name.find(kw) != std::string::npos)
			{
				return bindings[i];
			}
		}
	}

	return NULL;
}

/*
	glTF texture loading class
*/


void vkglTF::Texture::destroy()
{
	image = nullptr;
}

static bool fileExists(const std::string& filename)
{
	std::ifstream f(filename.c_str());
	return !f.fail();
}

void exitFatal(const std::string& message, int32_t exitCode)
{
#if defined(_WIN32)
	//if (!errorModeSilent) {
		MessageBox(NULL, message.c_str(), NULL, MB_OK | MB_ICONERROR);
	//}
#elif defined(__ANDROID__)
	LOGE("Fatal error: %s", message.c_str());
	vks::android::showAlert(message.c_str());
#endif
	//std::cerr << message << "\n";
#if !defined(__ANDROID__)
	exit(exitCode);
#endif
}


static void setImageLayout(
	VkCommandBuffer cmdbuffer,
	VkImage image,
	VkImageLayout oldImageLayout,
	VkImageLayout newImageLayout,
	VkImageSubresourceRange subresourceRange,
	VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
	VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT)
{
	// Create an image barrier object
	VkImageMemoryBarrier imageMemoryBarrier{};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	imageMemoryBarrier.oldLayout = oldImageLayout;
	imageMemoryBarrier.newLayout = newImageLayout;
	imageMemoryBarrier.image = image;
	imageMemoryBarrier.subresourceRange = subresourceRange;

	// Source layouts (old)
	// Source access mask controls actions that have to be finished on the old layout
	// before it will be transitioned to the new layout
	switch (oldImageLayout)
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
		// Image layout is undefined (or does not matter)
		// Only valid as initial layout
		// No flags required, listed only for completeness
		imageMemoryBarrier.srcAccessMask = 0;
		break;

	case VK_IMAGE_LAYOUT_PREINITIALIZED:
		// Image is preinitialized
		// Only valid as initial layout for linear images, preserves memory contents
		// Make sure host writes have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Image is a color attachment
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Image is a depth/stencil attachment
		// Make sure any writes to the depth/stencil buffer have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Image is a transfer source
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Image is a transfer destination
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Image is read by a shader
		// Make sure any shader reads from the image have been finished
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		// Other source layouts aren't handled (yet)
		break;
	}

	// Target layouts (new)
	// Destination access mask controls the dependency for the new image layout
	switch (newImageLayout)
	{
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		// Image will be used as a transfer destination
		// Make sure any writes to the image have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		// Image will be used as a transfer source
		// Make sure any reads from the image have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		break;

	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		// Image will be used as a color attachment
		// Make sure any writes to the color buffer have been finished
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Image layout will be used as a depth/stencil attachment
		// Make sure any writes to depth/stencil buffer have been finished
		imageMemoryBarrier.dstAccessMask = imageMemoryBarrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;

	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		// Image will be read in a shader (sampler, input attachment)
		// Make sure any writes to the image have been finished
		if (imageMemoryBarrier.srcAccessMask == 0)
		{
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		}
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		break;
	default:
		// Other source layouts aren't handled (yet)
		break;
	}

	// Put barrier inside setup command buffer
	vkCmdPipelineBarrier(
		cmdbuffer,
		srcStageMask,
		dstStageMask,
		0,
		0, nullptr,
		0, nullptr,
		1, &imageMemoryBarrier);
}

void vkglTF::Texture::fromglTfImage(tinygltf::Image& gltfimage, std::string path, gvk::ptr<gvk::Context> ctx)
{

	bool isKtx = false;
	// Image points to an external ktx file
	if (gltfimage.uri.find_last_of(".") != std::string::npos) {
		if (gltfimage.uri.substr(gltfimage.uri.find_last_of(".") + 1) == "ktx") {
			isKtx = true;
		}
	}

	VkFormat format;
	uint32_t width;
	uint32_t height;
	uint32_t mipLevels;

	if (!isKtx) {
		// Texture was loaded using STB_Image

		unsigned char* buffer = nullptr;
		VkDeviceSize bufferSize = 0;
		bool deleteBuffer = false;
		if (gltfimage.component == 3) {
			// Most devices don't support RGB only on Vulkan so convert if necessary
			// TODO: Check actual format support and transform only if required
			bufferSize = gltfimage.width * gltfimage.height * 4;
			buffer = new unsigned char[bufferSize];
			unsigned char* rgba = buffer;
			unsigned char* rgb = &gltfimage.image[0];
			for (size_t i = 0; i < gltfimage.width * gltfimage.height; ++i) {
				for (int32_t j = 0; j < 3; ++j) {
					rgba[j] = rgb[j];
				}
				rgba += 4;
				rgb += 3;
			}
			deleteBuffer = true;
		}
		else {
			buffer = &gltfimage.image[0];
			bufferSize = gltfimage.image.size();
		}

		format = VK_FORMAT_R8G8B8A8_UNORM;

		VkFormatProperties formatProperties;

		width = gltfimage.width;
		height = gltfimage.height;
		mipLevels = static_cast<uint32_t>(floor(log2(std::max(width, height))) + 1.0);

		gvk::ptr<gvk::Buffer> stagingBuffer = ctx->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bufferSize, GVK_HOST_WRITE_SEQUENTIAL).value();
		stagingBuffer->Write(buffer, 0, bufferSize);

		GvkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		image = ctx->CreateImage(imageCreateInfo).value();

		auto presentQueue = ctx->PresentQueue();

		auto imageCopyCmd = [&](VkCommandBuffer cmd)
		{
			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.levelCount = 1;
			subresourceRange.layerCount = 1;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = 0;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.image = image->GetImage();
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}

			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = width;
			bufferCopyRegion.imageExtent.height = height;
			bufferCopyRegion.imageExtent.depth = 1;

			vkCmdCopyBufferToImage(cmd, stagingBuffer->GetBuffer(), image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				imageMemoryBarrier.image = image->GetImage();
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}
		};
		presentQueue->SubmitTemporalCommand(imageCopyCmd, gvk::SemaphoreInfo(), NULL, true );

		stagingBuffer = nullptr;

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipLevels;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;

		auto blitImageCmd = [&](VkCommandBuffer cmd)
		{
			for (uint32_t i = 1; i < mipLevels; i++) {
				VkImageBlit imageBlit{};

				imageBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.srcSubresource.layerCount = 1;
				imageBlit.srcSubresource.mipLevel = i - 1;
				imageBlit.srcOffsets[1].x = int32_t(width >> (i - 1));
				imageBlit.srcOffsets[1].y = int32_t(height >> (i - 1));
				imageBlit.srcOffsets[1].z = 1;

				imageBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				imageBlit.dstSubresource.layerCount = 1;
				imageBlit.dstSubresource.mipLevel = i;
				imageBlit.dstOffsets[1].x = int32_t(width >> i);
				imageBlit.dstOffsets[1].y = int32_t(height >> i);
				imageBlit.dstOffsets[1].z = 1;

				VkImageSubresourceRange mipSubRange = {};
				mipSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				mipSubRange.baseMipLevel = i;
				mipSubRange.levelCount = 1;
				mipSubRange.layerCount = 1;

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = 0;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.image = image->GetImage();
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}

				vkCmdBlitImage(cmd, image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageBlit, VK_FILTER_LINEAR);

				{
					VkImageMemoryBarrier imageMemoryBarrier{};
					imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
					imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
					imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
					imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
					imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
					imageMemoryBarrier.image = image->GetImage();
					imageMemoryBarrier.subresourceRange = mipSubRange;
					vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
				}
			}

			subresourceRange.levelCount = mipLevels;

			{
				VkImageMemoryBarrier imageMemoryBarrier{};
				imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				imageMemoryBarrier.image = image->GetImage();
				imageMemoryBarrier.subresourceRange = subresourceRange;
				vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
			}
		};

		if (mipLevels > 1)
		{
			presentQueue->SubmitTemporalCommand(blitImageCmd, gvk::SemaphoreInfo(), NULL, true);
		}
		
	}
	else {
		// Texture is stored in an external ktx file
		std::string filename = path + "/" + gltfimage.uri;

		ktxTexture* ktxTexture;

		ktxResult result = KTX_SUCCESS;

		if (!fileExists(filename)) {
			exitFatal("Could not load texture from " + filename + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
		}
		result = ktxTexture_CreateFromNamedFile(filename.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
		assert(result == KTX_SUCCESS);

		width = ktxTexture->baseWidth;
		height = ktxTexture->baseHeight;
		mipLevels = ktxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture->dataSize;
		// @todo: Use ktxTexture_GetVkFormat(ktxTexture)
		format = ktxTexture_GetVkFormat(ktxTexture);


		//VkCommandBuffer copyCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		//VkBuffer stagingBuffer;
		//VkDeviceMemory stagingMemory;

		gvk::ptr<gvk::Buffer> stagingBuffer = ctx->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ktxTextureSize, GVK_HOST_WRITE_SEQUENTIAL).value();
		stagingBuffer->Write(ktxTextureData, 0, ktxTextureSize);


		std::vector<VkBufferImageCopy> bufferCopyRegions;
		for (uint32_t i = 0; i < mipLevels; i++)
		{
			ktx_size_t offset;
			KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, i, 0, 0, &offset);
			assert(result == KTX_SUCCESS);
			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = i;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = std::max(1u, ktxTexture->baseWidth >> i);
			bufferCopyRegion.imageExtent.height = std::max(1u, ktxTexture->baseHeight >> i);
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = offset;
			bufferCopyRegions.push_back(bufferCopyRegion);
		}


		// Create optimal tiled target image
		GvkImageCreateInfo imageCreateInfo{};
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width, height, 1 };
		imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		image = ctx->CreateImage(imageCreateInfo).value();
		
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipLevels;
		subresourceRange.layerCount = 1;

		auto copyCmd = [&](VkCommandBuffer cmd)
		{
			setImageLayout(cmd, image->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
			vkCmdCopyBufferToImage(cmd, stagingBuffer->GetBuffer(), image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
			setImageLayout(cmd, image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
		};

		gvk::ptr<gvk::CommandQueue> presentQueue = ctx->PresentQueue();
		presentQueue->SubmitTemporalCommand(copyCmd, gvk::SemaphoreInfo(), NULL, true);

		ktxTexture_Destroy(ktxTexture);
	}

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.maxAnisotropy = 1.0;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxLod = (float)mipLevels;
	samplerInfo.maxAnisotropy = 8.0f;
	sampler = ctx->CreateSampler(samplerInfo).value();

	mainView = image->CreateView(VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1, VK_IMAGE_VIEW_TYPE_2D).value();
}

/*
	glTF material
*/


/*
	glTF primitive
*/
void vkglTF::Primitive::setDimensions(glm::vec3 min, glm::vec3 max) {
	dimensions.min = min;
	dimensions.max = max;
	dimensions.size = max - min;
	dimensions.center = (min + max) / 2.0f;
	dimensions.radius = glm::distance(min, max) / 2.0f;
}

/*
	glTF mesh
*/


void vkglTF::Mesh::UpdateUBO(glm::mat4 m)
{
	glm::mat4 invTransModel = glm::transpose(glm::inverse(m));
	ubo.invTransModel = invTransModel;
	ubo.model = m;

	uniformBuffer->Write(&ubo, 0, sizeof(ubo));
}

void vkglTF::Mesh::createMeshObjectUBO(gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout)
{
	if (targetLayout == layout) return;
	targetLayout = layout;

	uniformBufferDesc = alloc->Allocate(layout).value();
	
	std::vector<std::string> keyWords = {"object"};
	SpvReflectDescriptorBinding* binding = fliterDescriptorSetLayoutBinding(layout, keyWords);
	vkrg_assert(binding != NULL);

	GvkDescriptorSetWrite update;
	update.BufferWrite(uniformBufferDesc, (VkDescriptorType)binding->descriptor_type, binding->binding,
		uniformBuffer->GetBuffer(), 0, uniformBuffer->GetSize())
		.Emit(ctx->GetDevice());
}

vkglTF::Mesh::Mesh(gvk::ptr<gvk::Context> ctx, glm::mat4 model)
{
	this->ctx = ctx;
	uniformBuffer = ctx->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(Mesh::ObjectUBO), GVK_HOST_WRITE_SEQUENTIAL).value();
	UpdateUBO(model);
}

vkglTF::Mesh::~Mesh() {
	uniformBuffer = nullptr;
}

/*
	glTF node
*/
glm::mat4 vkglTF::Node::localMatrix() {
	return glm::translate(glm::mat4(1.0f), translation) * glm::mat4(rotation) * glm::scale(glm::mat4(1.0f), scale) * matrix;
}

glm::mat4 vkglTF::Node::getMatrix() {
	glm::mat4 m = localMatrix();
	vkglTF::Node *p = parent;
	while (p) {
		m = p->localMatrix() * m;
		p = p->parent;
	}
	return m;
}

void vkglTF::Node::update() {
	if (mesh) {
		glm::mat4 m = getMatrix();
		if (skin) {
			/*
			for (size_t i = 0; i < skin->joints.size(); i++) {
				vkglTF::Node *jointNode = skin->joints[i];
				glm::mat4 jointMat = jointNode->getMatrix() * skin->inverseBindMatrices[i];
				jointMat = inverseModel * jointMat;
				mesh->uniformBlock.jointMatrix[i] = jointMat;
			}
			
			mesh->uniformBlock.jointcount = (float)skin->joints.size();
			memcpy(mesh->uniformBuffer.mapped, &mesh->uniformBlock, sizeof(mesh->uniformBlock));
			*/
			mesh->UpdateUBO(m);
		} else {
			//memcpy(mesh->uniformBuffer.mapped, &m, sizeof(glm::mat4));
		}
	}

	for (auto& child : children) {
		child->update();
	}
}

vkglTF::Node::~Node() {
	if (mesh) {
		delete mesh;
	}
	for (auto& child : children) {
		delete child;
	}
}

/*
	glTF default vertex layout with easy Vulkan mapping functions
*/

VkVertexInputBindingDescription vkglTF::Vertex::vertexInputBindingDescription;
std::vector<VkVertexInputAttributeDescription> vkglTF::Vertex::vertexInputAttributeDescriptions;
VkPipelineVertexInputStateCreateInfo vkglTF::Vertex::pipelineVertexInputStateCreateInfo;

VkVertexInputBindingDescription vkglTF::Vertex::inputBindingDescription(uint32_t binding) {
	return VkVertexInputBindingDescription({ binding, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX });
}

VkVertexInputAttributeDescription vkglTF::Vertex::inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component) {
	switch (component) {
		case VertexComponent::Position: 
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos) });
		case VertexComponent::Normal:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) });
		case VertexComponent::UV:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv) });
		case VertexComponent::Color:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, color) });
		case VertexComponent::Tangent:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)} );
		
		case VertexComponent::Joint0:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, joint0) });
		case VertexComponent::Weight0:
			return VkVertexInputAttributeDescription({ location, binding, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weight0) });
		

		default:
			return VkVertexInputAttributeDescription({});
	}
}

std::vector<VkVertexInputAttributeDescription> vkglTF::Vertex::inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components) {
	std::vector<VkVertexInputAttributeDescription> result;
	uint32_t location = 0;
	for (VertexComponent component : components) {
		result.push_back(Vertex::inputAttributeDescription(binding, location, component));
		location++;
	}
	return result;
}

/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
VkPipelineVertexInputStateCreateInfo* vkglTF::Vertex::getPipelineVertexInputState(const std::vector<VertexComponent> components) {
	vertexInputBindingDescription = Vertex::inputBindingDescription(0);
	Vertex::vertexInputAttributeDescriptions = Vertex::inputAttributeDescriptions(0, components);
	pipelineVertexInputStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	pipelineVertexInputStateCreateInfo.vertexBindingDescriptionCount = 1;
	pipelineVertexInputStateCreateInfo.pVertexBindingDescriptions = &Vertex::vertexInputBindingDescription;
	pipelineVertexInputStateCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(Vertex::vertexInputAttributeDescriptions.size());
	pipelineVertexInputStateCreateInfo.pVertexAttributeDescriptions = Vertex::vertexInputAttributeDescriptions.data();
	return &pipelineVertexInputStateCreateInfo;
}

vkglTF::Texture* vkglTF::Model::getTexture(uint32_t index)
{

	if (index < textures.size()) {
		return &textures[index];
	}
	return nullptr;
}

void vkglTF::Model::createEmptyTexture(gvk::ptr<gvk::Context> ctx,gvk::ptr<gvk::CommandQueue> queue)
{
	GvkImageCreateInfo info{};
	info.arrayLayers = 1;
	info.mipLevels = 1;
	info.extent.width = 1;
	info.extent.height = 1;
	info.extent.depth = 1;
	info.flags = 0;
	info.format = VK_FORMAT_R8G8B8A8_UNORM;
	info.imageType = VK_IMAGE_TYPE_2D;
	info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	info.samples = VK_SAMPLE_COUNT_1_BIT;
	info.tiling = VK_IMAGE_TILING_LINEAR;
	info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	size_t bufferSize = info.extent.width * info.extent.height * 4;
	unsigned char* buffer = new unsigned char[bufferSize];
	memset(buffer, 0, bufferSize);

	buffer[0] = 255;
	buffer[1] = 255;
	buffer[2] = 255;
	buffer[3] = 255;

	gvk::ptr<gvk::Buffer> stagingBuffer = ctx->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bufferSize, GVK_HOST_WRITE_SEQUENTIAL).value();
	stagingBuffer->Write(buffer, 0, bufferSize);

	VkBufferImageCopy bufferCopyRegion = {};
	bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	bufferCopyRegion.imageSubresource.layerCount = 1;
	bufferCopyRegion.imageExtent.width = info.extent.width;
	bufferCopyRegion.imageExtent.height = info.extent.height;
	bufferCopyRegion.imageExtent.depth = 1;

	gvk::ptr<gvk::Image> image = ctx->CreateImage(info).value();

	VkImageSubresourceRange subresourceRange{};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.layerCount = 1;

	auto copy = [&](VkCommandBuffer cmd)
	{
		setImageLayout(cmd, image->GetImage(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
		vkCmdCopyBufferToImage(cmd, stagingBuffer->GetBuffer(), image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);
		setImageLayout(cmd, image->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
	};
	ctx->PresentQueue()->SubmitTemporalCommand(copy, gvk::SemaphoreInfo(), NULL, true);

	vkglTF::Texture texture;
	texture.image = image;
	
	VkSamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
	samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
	samplerCreateInfo.maxAnisotropy = 1.0f;
	texture.sampler = ctx->CreateSampler(samplerCreateInfo).value();

	texture.mainView = image->CreateView(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1, VK_IMAGE_VIEW_TYPE_2D).value();

	emptyTexture = texture;

	stagingBuffer = nullptr;
}

/*
	glTF model loading and rendering class
*/
vkglTF::Model::~Model()
{
	vertices.buffer = nullptr;
	indices.buffer = nullptr;

	for (auto texture : textures) {
		texture.destroy();
	}
	for (auto node : nodes) {
		delete node;
	}
    for (auto skin : skins) {
        delete skin;
    }
	emptyTexture.destroy();
}

void vkglTF::Model::loadNode(vkglTF::Node *parent, const tinygltf::Node &node, uint32_t nodeIndex, const tinygltf::Model &model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale)
{
	vkglTF::Node *newNode = new Node{};
	newNode->index = nodeIndex;
	newNode->parent = parent;
	newNode->name = node.name;
	newNode->skinIndex = node.skin;
	newNode->matrix = glm::mat4(1.0f);

	// Generate local node matrix
	glm::vec3 translation = glm::vec3(0.0f);
	if (node.translation.size() == 3) {
		translation = glm::make_vec3(node.translation.data());
		newNode->translation = translation;
	}
	glm::mat4 rotation = glm::mat4(1.0f);
	if (node.rotation.size() == 4) {
		glm::quat q = glm::make_quat(node.rotation.data());
		newNode->rotation = glm::mat4(q);
	}
	glm::vec3 scale = glm::vec3(1.0f);
	if (node.scale.size() == 3) {
		scale = glm::make_vec3(node.scale.data());
		newNode->scale = scale;
	}
	if (node.matrix.size() == 16) {
		newNode->matrix = glm::make_mat4x4(node.matrix.data());
		if (globalscale != 1.0f) {
			//newNode->matrix = glm::scale(newNode->matrix, glm::vec3(globalscale));
		}
	};

	// Node with children
	if (node.children.size() > 0) {
		for (auto i = 0; i < node.children.size(); i++) {
			loadNode(newNode, model.nodes[node.children[i]], node.children[i], model, indexBuffer, vertexBuffer, globalscale);
		}
	}

	// Node contains mesh data
	if (node.mesh > -1) {
		const tinygltf::Mesh mesh = model.meshes[node.mesh];
		Mesh *newMesh = new Mesh(ctx, newNode->matrix);
		newMesh->name = mesh.name;
		for (size_t j = 0; j < mesh.primitives.size(); j++) {
			const tinygltf::Primitive &primitive = mesh.primitives[j];
			if (primitive.indices < 0) {
				continue;
			}
			uint32_t indexStart = static_cast<uint32_t>(indexBuffer.size());
			uint32_t vertexStart = static_cast<uint32_t>(vertexBuffer.size());
			uint32_t indexCount = 0;
			uint32_t vertexCount = 0;
			glm::vec3 posMin{};
			glm::vec3 posMax{};
			bool hasSkin = false;
			// Vertices
			{
				const float *bufferPos = nullptr;
				const float *bufferNormals = nullptr;
				const float *bufferTexCoords = nullptr;
				const float* bufferColors = nullptr;
				const float *bufferTangents = nullptr;
				uint32_t numColorComponents;
				const uint16_t *bufferJoints = nullptr;
				const float *bufferWeights = nullptr;

				// Position attribute is required
				assert(primitive.attributes.find("POSITION") != primitive.attributes.end());

				const tinygltf::Accessor &posAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
				const tinygltf::BufferView &posView = model.bufferViews[posAccessor.bufferView];
				bufferPos = reinterpret_cast<const float *>(&(model.buffers[posView.buffer].data[posAccessor.byteOffset + posView.byteOffset]));
				posMin = glm::vec3(posAccessor.minValues[0], posAccessor.minValues[1], posAccessor.minValues[2]);
				posMax = glm::vec3(posAccessor.maxValues[0], posAccessor.maxValues[1], posAccessor.maxValues[2]);

				if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
					const tinygltf::Accessor &normAccessor = model.accessors[primitive.attributes.find("NORMAL")->second];
					const tinygltf::BufferView &normView = model.bufferViews[normAccessor.bufferView];
					bufferNormals = reinterpret_cast<const float *>(&(model.buffers[normView.buffer].data[normAccessor.byteOffset + normView.byteOffset]));
				}

				if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("TEXCOORD_0")->second];
					const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
					bufferTexCoords = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				if (primitive.attributes.find("COLOR_0") != primitive.attributes.end())
				{
					const tinygltf::Accessor& colorAccessor = model.accessors[primitive.attributes.find("COLOR_0")->second];
					const tinygltf::BufferView& colorView = model.bufferViews[colorAccessor.bufferView];
					// Color buffer are either of type vec3 or vec4
					numColorComponents = colorAccessor.type == TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 ? 3 : 4;
					bufferColors = reinterpret_cast<const float*>(&(model.buffers[colorView.buffer].data[colorAccessor.byteOffset + colorView.byteOffset]));
				}

				if (primitive.attributes.find("TANGENT") != primitive.attributes.end())
				{
					const tinygltf::Accessor &tangentAccessor = model.accessors[primitive.attributes.find("TANGENT")->second];
					const tinygltf::BufferView &tangentView = model.bufferViews[tangentAccessor.bufferView];
					bufferTangents = reinterpret_cast<const float *>(&(model.buffers[tangentView.buffer].data[tangentAccessor.byteOffset + tangentView.byteOffset]));
				}

				// Skinning
				// Joints
				if (primitive.attributes.find("JOINTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &jointAccessor = model.accessors[primitive.attributes.find("JOINTS_0")->second];
					const tinygltf::BufferView &jointView = model.bufferViews[jointAccessor.bufferView];
					bufferJoints = reinterpret_cast<const uint16_t *>(&(model.buffers[jointView.buffer].data[jointAccessor.byteOffset + jointView.byteOffset]));
				}

				if (primitive.attributes.find("WEIGHTS_0") != primitive.attributes.end()) {
					const tinygltf::Accessor &uvAccessor = model.accessors[primitive.attributes.find("WEIGHTS_0")->second];
					const tinygltf::BufferView &uvView = model.bufferViews[uvAccessor.bufferView];
					bufferWeights = reinterpret_cast<const float *>(&(model.buffers[uvView.buffer].data[uvAccessor.byteOffset + uvView.byteOffset]));
				}

				hasSkin = (bufferJoints && bufferWeights);

				vertexCount = static_cast<uint32_t>(posAccessor.count);

				for (size_t v = 0; v < posAccessor.count; v++) {
					Vertex vert{};
					vert.pos = glm::vec4(glm::make_vec3(&bufferPos[v * 3]), 1.0f);
					vert.normal = glm::normalize(glm::vec3(bufferNormals ? glm::make_vec3(&bufferNormals[v * 3]) : glm::vec3(0.0f)));
					vert.uv = bufferTexCoords ? glm::make_vec2(&bufferTexCoords[v * 2]) : glm::vec3(0.0f);
					if (bufferColors) {
						switch (numColorComponents) {
							case 3: 
								vert.color = glm::vec4(glm::make_vec3(&bufferColors[v * 3]), 1.0f);
							case 4:
								vert.color = glm::make_vec4(&bufferColors[v * 4]);
						}
					}
					else {
						vert.color = glm::vec4(1.0f);
					}
					vert.tangent = bufferTangents ? glm::vec4(glm::make_vec4(&bufferTangents[v * 4])) : glm::vec4(0.0f);
					vert.joint0 = hasSkin ? glm::vec4(glm::make_vec4(&bufferJoints[v * 4])) : glm::vec4(0.0f);
					vert.weight0 = hasSkin ? glm::make_vec4(&bufferWeights[v * 4]) : glm::vec4(0.0f);
					vertexBuffer.push_back(vert);
				}
			}
			// Indices
			{
				const tinygltf::Accessor &accessor = model.accessors[primitive.indices];
				const tinygltf::BufferView &bufferView = model.bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

				indexCount = static_cast<uint32_t>(accessor.count);

				switch (accessor.componentType) {
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
					uint32_t *buf = new uint32_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint32_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
                    delete[] buf;
					break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
					uint16_t *buf = new uint16_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint16_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
                    delete[] buf;
                    break;
				}
				case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
					uint8_t *buf = new uint8_t[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(uint8_t));
					for (size_t index = 0; index < accessor.count; index++) {
						indexBuffer.push_back(buf[index] + vertexStart);
					}
                    delete[] buf;
                    break;
				}
				default:
					std::cout << "Index component type " << accessor.componentType << " not supported!" << std::endl;
					vkrg_assert(false);
					return;
				}
			}
			Primitive *newPrimitive = new Primitive(indexStart, indexCount, primitive.material > -1 ? materials[primitive.material] : materials.back());
			newPrimitive->firstVertex = vertexStart;
			newPrimitive->vertexCount = vertexCount;
			newPrimitive->setDimensions(posMin, posMax);
			newMesh->primitives.push_back(newPrimitive);
		}
		newNode->mesh = newMesh;
	}
	if (parent) {
		parent->children.push_back(newNode);
	} else {
		nodes.push_back(newNode);
	}
	linearNodes.push_back(newNode);
}

void vkglTF::Model::loadSkins(tinygltf::Model &gltfModel)
{
	for (tinygltf::Skin &source : gltfModel.skins) {
		Skin *newSkin = new Skin{};
		newSkin->name = source.name;
				
		// Find skeleton root node
		if (source.skeleton > -1) {
			newSkin->skeletonRoot = nodeFromIndex(source.skeleton);
		}

		// Find joint nodes
		for (int jointIndex : source.joints) {
			Node* node = nodeFromIndex(jointIndex);
			if (node) {
				newSkin->joints.push_back(nodeFromIndex(jointIndex));
			}
		}

		// Get inverse bind matrices from buffer
		if (source.inverseBindMatrices > -1) {
			const tinygltf::Accessor &accessor = gltfModel.accessors[source.inverseBindMatrices];
			const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
			const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];
			newSkin->inverseBindMatrices.resize(accessor.count);
			memcpy(newSkin->inverseBindMatrices.data(), &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::mat4));
		}

		skins.push_back(newSkin);
	}
}

void vkglTF::Model::loadImages(tinygltf::Model &gltfModel, gvk::ptr<gvk::Context> ctx)
{
	for (tinygltf::Image &image : gltfModel.images) {
		vkglTF::Texture texture;
		texture.fromglTfImage(image, path, ctx);
		textures.push_back(texture);
	}
	// Create an empty texture to be used for empty material images
	createEmptyTexture(ctx, ctx->PresentQueue());
}

void vkglTF::Model::loadMaterials(tinygltf::Model &gltfModel)
{
	for (tinygltf::Material& mat : gltfModel.materials) {
		vkglTF::Material material{};
		if (mat.values.find("baseColorTexture") != mat.values.end()) {
			material.baseColorTexture = getTexture(gltfModel.textures[mat.values["baseColorTexture"].TextureIndex()].source);
		}
		// Metallic roughness workflow
		if (mat.values.find("metallicRoughnessTexture") != mat.values.end()) {
			material.metallicRoughnessTexture = getTexture(gltfModel.textures[mat.values["metallicRoughnessTexture"].TextureIndex()].source);
		}
		if (mat.values.find("roughnessFactor") != mat.values.end()) {
			material.roughnessFactor = static_cast<float>(mat.values["roughnessFactor"].Factor());
		}
		if (mat.values.find("metallicFactor") != mat.values.end()) {
			material.metallicFactor = static_cast<float>(mat.values["metallicFactor"].Factor());
		}
		if (mat.values.find("baseColorFactor") != mat.values.end()) {
			material.baseColorFactor = glm::make_vec4(mat.values["baseColorFactor"].ColorFactor().data());
		}				
		if (mat.additionalValues.find("normalTexture") != mat.additionalValues.end()) {
			material.normalTexture = getTexture(gltfModel.textures[mat.additionalValues["normalTexture"].TextureIndex()].source);
		} else {
			material.normalTexture = &emptyTexture;
		}
		if (mat.additionalValues.find("emissiveTexture") != mat.additionalValues.end()) {
			material.emissiveTexture = getTexture(gltfModel.textures[mat.additionalValues["emissiveTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("occlusionTexture") != mat.additionalValues.end()) {
			material.occlusionTexture = getTexture(gltfModel.textures[mat.additionalValues["occlusionTexture"].TextureIndex()].source);
		}
		if (mat.additionalValues.find("alphaMode") != mat.additionalValues.end()) {
			tinygltf::Parameter param = mat.additionalValues["alphaMode"];
			if (param.string_value == "BLEND") {
				material.alphaMode = Material::ALPHAMODE_BLEND;
			}
			if (param.string_value == "MASK") {
				material.alphaMode = Material::ALPHAMODE_MASK;
			}
		}
		if (mat.additionalValues.find("alphaCutoff") != mat.additionalValues.end()) {
			material.alphaCutoff = static_cast<float>(mat.additionalValues["alphaCutoff"].Factor());
		}

		materials.push_back(material);
	}
	// Push a default material at the end of the list for meshes with no material assigned
	materials.push_back(Material());
}

void vkglTF::Model::loadAnimations(tinygltf::Model &gltfModel)
{
	for (tinygltf::Animation &anim : gltfModel.animations) {
		vkglTF::Animation animation{};
		animation.name = anim.name;
		if (anim.name.empty()) {
			animation.name = std::to_string(animations.size());
		}

		// Samplers
		for (auto &samp : anim.samplers) {
			vkglTF::AnimationSampler sampler{};

			if (samp.interpolation == "LINEAR") {
				sampler.interpolation = AnimationSampler::InterpolationType::LINEAR;
			}
			if (samp.interpolation == "STEP") {
				sampler.interpolation = AnimationSampler::InterpolationType::STEP;
			}
			if (samp.interpolation == "CUBICSPLINE") {
				sampler.interpolation = AnimationSampler::InterpolationType::CUBICSPLINE;
			}

			// Read sampler input time values
			{
				const tinygltf::Accessor &accessor = gltfModel.accessors[samp.input];
				const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

				assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				float *buf = new float[accessor.count];
				memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(float));
				for (size_t index = 0; index < accessor.count; index++) {
					sampler.inputs.push_back(buf[index]);
				}
                delete[] buf;
				for (auto input : sampler.inputs) {
					if (input < animation.start) {
						animation.start = input;
					};
					if (input > animation.end) {
						animation.end = input;
					}
				}
			}

			// Read sampler output T/R/S values 
			{
				const tinygltf::Accessor &accessor = gltfModel.accessors[samp.output];
				const tinygltf::BufferView &bufferView = gltfModel.bufferViews[accessor.bufferView];
				const tinygltf::Buffer &buffer = gltfModel.buffers[bufferView.buffer];

				assert(accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT);

				switch (accessor.type) {
				case TINYGLTF_TYPE_VEC3: {
					glm::vec3 *buf = new glm::vec3[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec3));
					for (size_t index = 0; index < accessor.count; index++) {
						sampler.outputsVec4.push_back(glm::vec4(buf[index], 0.0f));
					}
                    delete[] buf;
                    break;
				}
				case TINYGLTF_TYPE_VEC4: {
					glm::vec4 *buf = new glm::vec4[accessor.count];
					memcpy(buf, &buffer.data[accessor.byteOffset + bufferView.byteOffset], accessor.count * sizeof(glm::vec4));
					for (size_t index = 0; index < accessor.count; index++) {
						sampler.outputsVec4.push_back(buf[index]);
					}
                    delete[] buf;
                    break;
				}
				default: {
					std::cout << "unknown type" << std::endl;
					break;
				}
				}
			}

			animation.samplers.push_back(sampler);
		}

		// Channels
		for (auto &source: anim.channels) {
			vkglTF::AnimationChannel channel{};

			if (source.target_path == "rotation") {
				channel.path = AnimationChannel::PathType::ROTATION;
			}
			if (source.target_path == "translation") {
				channel.path = AnimationChannel::PathType::TRANSLATION;
			}
			if (source.target_path == "scale") {
				channel.path = AnimationChannel::PathType::SCALE;
			}
			if (source.target_path == "weights") {
				std::cout << "weights not yet supported, skipping channel" << std::endl;
				continue;
			}
			channel.samplerIndex = source.sampler;
			channel.node = nodeFromIndex(source.target_node);
			if (!channel.node) {
				continue;
			}

			animation.channels.push_back(channel);
		}

		animations.push_back(animation);
	}
}

void vkglTF::Model::loadFromFile(std::string filename, gvk::ptr<gvk::Context> ctx, SpvReflectInterfaceVariable** variables, uint32_t variableCount, uint32_t fileLoadingFlags, float scale)
{
	tinygltf::Model gltfModel;
	tinygltf::TinyGLTF gltfContext;
	
	fileLoadingFlags |= FileLoadingFlags::PreTransformVertices;
	

	if (fileLoadingFlags & FileLoadingFlags::DontLoadImages) {
		gltfContext.SetImageLoader(loadImageDataFuncEmpty, nullptr);
	} else {
		gltfContext.SetImageLoader(loadImageDataFunc, nullptr);
	}
#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	size_t pos = filename.find_last_of('/');
	path = filename.substr(0, pos);

	std::string error, warning;

	this->ctx = ctx;

#if defined(__ANDROID__)
	// On Android all assets are packed with the apk in a compressed form, so we need to open them using the asset manager
	// We let tinygltf handle this, by passing the asset manager of our app
	tinygltf::asset_manager = androidApp->activity->assetManager;
#endif
	bool fileLoaded = gltfContext.LoadASCIIFromFile(&gltfModel, &error, &warning, filename);

	std::vector<uint32_t> indexBuffer;
	std::vector<Vertex> vertexBuffer;

	std::vector<SpvReflectInterfaceVariable*> sortedVariables(variableCount);
	for (uint32_t i = 0;i < variableCount;i++)
	{
		auto var = variables[i];
		sortedVariables[var->location] = var;
	}

	if (fileLoaded) {
		if (!(fileLoadingFlags & FileLoadingFlags::DontLoadImages)) {
			loadImages(gltfModel, ctx);
		}
		loadMaterials(gltfModel);
		const tinygltf::Scene &scene = gltfModel.scenes[gltfModel.defaultScene > -1 ? gltfModel.defaultScene : 0];
		for (size_t i = 0; i < scene.nodes.size(); i++) {
			const tinygltf::Node node = gltfModel.nodes[scene.nodes[i]];
			loadNode(nullptr, node, scene.nodes[i], gltfModel, indexBuffer, vertexBuffer, scale);
		}
		if (gltfModel.animations.size() > 0) {
			loadAnimations(gltfModel);
		}
		loadSkins(gltfModel);

		for (auto node : linearNodes) {
			// Assign skins
			if (node->skinIndex > -1) {
				node->skin = skins[node->skinIndex];
			}
			// Initial pose
			if (node->mesh) {
				node->update();
			}
		}
	}
	else {
		// TODO: throw
		exitFatal("Could not load glTF file \"" + filename + "\": " + error, -1);
		return;
	}

	// Pre-Calculations for requested features
	if ((fileLoadingFlags & FileLoadingFlags::PreTransformVertices) || (fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors) || (fileLoadingFlags & FileLoadingFlags::FlipY)) {
		const bool preTransform = fileLoadingFlags & FileLoadingFlags::PreTransformVertices;
		const bool preMultiplyColor = fileLoadingFlags & FileLoadingFlags::PreMultiplyVertexColors;
		const bool flipY = fileLoadingFlags & FileLoadingFlags::FlipY;
		for (Node* node : linearNodes) {
			if (node->mesh) {
				const glm::mat4 localMatrix = node->getMatrix();
				for (Primitive* primitive : node->mesh->primitives) {
					for (uint32_t i = 0; i < primitive->vertexCount; i++) {
						Vertex& vertex = vertexBuffer[primitive->firstVertex + i];
						// Pre-transform vertex positions by node-hierarchy
						if (preTransform) {
							vertex.pos = glm::vec3(localMatrix * glm::vec4(vertex.pos, 1.0f));
							vertex.normal = glm::normalize(glm::mat3(localMatrix) * vertex.normal);
						}
						// Flip Y-Axis of vertex positions
						if (flipY) {
							vertex.pos.y *= -1.0f;
							vertex.normal.y *= -1.0f;
						}
						// Pre-Multiply vertex colors with material base color
						if (preMultiplyColor) {
							vertex.color = primitive->material.baseColorFactor * vertex.color;
						}
					}
				}
			}
		}
	}

	for (auto extension : gltfModel.extensionsUsed) {
		if (extension == "KHR_materials_pbrSpecularGlossiness") {
			std::cout << "Required extension: " << extension;
			metallicRoughnessWorkflow = false;
		}
	}

	std::vector<uint32_t> attributeOffset((uint32_t)VertexComponent::MaxEnum, 0);
	std::vector<uint32_t> attributeStride((uint32_t)VertexComponent::MaxEnum, 0);
	std::vector<bool> attributeUsed((uint32_t)VertexComponent::MaxEnum, false);

	

	uint32_t total_offset = 0;
	for (uint32_t i = 0;i < variableCount;i++)
	{
		SpvReflectInterfaceVariable* var = sortedVariables[i];
		uint32_t size = gvk::GetFormatSize((VkFormat)var->format);
		std::string name = strToLower(var->name);
		
		VertexComponent comp = VertexComponent::MaxEnum;
		if (name.find("color") != std::string::npos)
		{
			comp = VertexComponent::Color;
		}
		else if (name.find("normal") != std::string::npos)
		{
			comp = VertexComponent::Normal;
		}
		else if (name.find("tangent") != std::string::npos)
		{
			comp = VertexComponent::Tangent;
		}
		else if (name.find("uv") != std::string::npos)
		{
			comp = VertexComponent::UV;
		}
		else if(name.find("position"))
		{
			comp = VertexComponent::Position;
		}
		else
		{
			vkrg_assert(false);
		}

		attributeOffset[(uint32_t)comp] = total_offset;
		attributeStride[(uint32_t)comp] = size;
		attributeUsed[(uint32_t)comp] = true;

		total_offset += size;
	}

	std::vector<uint8_t> assambledVertexBuffer(vertexBuffer.size() * total_offset, 0);

	for (uint32_t i = 0;i < vertexBuffer.size();i++)
	{
		uint32_t vertexOffset = i * total_offset;
		if (attributeUsed[(uint32_t)VertexComponent::Position])
		{
			*(glm::vec3*)(assambledVertexBuffer.data() + vertexOffset + attributeOffset[(uint32_t)VertexComponent::Position]) = vertexBuffer[i].pos;
		}
		if (attributeUsed[(uint32_t)VertexComponent::Color])
		{
			*(glm::vec4*)(assambledVertexBuffer.data() + vertexOffset + attributeOffset[(uint32_t)VertexComponent::Color]) = vertexBuffer[i].color;
		}
		if (attributeUsed[(uint32_t)VertexComponent::Normal])
		{
			*(glm::vec3*)(assambledVertexBuffer.data() + vertexOffset + attributeOffset[(uint32_t)VertexComponent::Normal]) = vertexBuffer[i].normal;
		}
		if (attributeUsed[(uint32_t)VertexComponent::Tangent])
		{
			*(glm::vec3*)(assambledVertexBuffer.data() + vertexOffset + attributeOffset[(uint32_t)VertexComponent::Tangent]) = vertexBuffer[i].tangent;
		}
		if (attributeUsed[(uint32_t)VertexComponent::UV])
		{
			*(glm::vec2*)(assambledVertexBuffer.data() + vertexOffset + attributeOffset[(uint32_t)VertexComponent::UV]) = vertexBuffer[i].uv;
		}
	}


	size_t vertexBufferSize = assambledVertexBuffer.size();
	size_t indexBufferSize = indexBuffer.size() * sizeof(uint32_t);
	indices.count = static_cast<uint32_t>(indexBuffer.size());
	vertices.count = static_cast<uint32_t>(vertexBuffer.size());

	assert((vertexBufferSize > 0) && (indexBufferSize > 0));

	gvk::ptr<gvk::Buffer> vertexStaging = ctx->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexBufferSize, GVK_HOST_WRITE_SEQUENTIAL).value();
	gvk::ptr<gvk::Buffer> indexStaging = ctx->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, indexBufferSize, GVK_HOST_WRITE_SEQUENTIAL).value();

	vertexStaging->Write(assambledVertexBuffer.data(), 0, vertexBufferSize);
	indexStaging->Write(indexBuffer.data(), 0, indexBufferSize);

	vertices.buffer = ctx->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vertexBufferSize, GVK_HOST_WRITE_NONE).value();
	indices.buffer = ctx->CreateBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, indexBufferSize, GVK_HOST_WRITE_NONE).value();


	auto copy = [&](VkCommandBuffer copyCmd)
	{
		VkBufferCopy copyRegion = {};
		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(copyCmd, vertexStaging->GetBuffer(), vertices.buffer->GetBuffer(), 1, &copyRegion);

		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(copyCmd, indexStaging->GetBuffer(), indices.buffer->GetBuffer(), 1, &copyRegion);
	};
	ctx->PresentQueue()->SubmitTemporalCommand(copy, gvk::SemaphoreInfo(), NULL, true);

	vertexStaging = nullptr;
	indexStaging = nullptr;

	getSceneDimensions();

	// Setup descriptors
	uint32_t uboCount{ 0 };
	uint32_t imageCount{ 0 };
	for (auto node : linearNodes) {
		if (node->mesh) {
			uboCount++;
		}
	}
	for (auto material : materials) {
		if (material.baseColorTexture != nullptr) {
			imageCount++;
		}
	}
	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uboCount },
	};
}

void vkglTF::Model::bindBuffers(VkCommandBuffer commandBuffer)
{
	const VkDeviceSize offsets[1] = {0};
	VkBuffer verticeBuffers[] = { vertices.buffer->GetBuffer() };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, verticeBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, indices.buffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
	buffersBound = true;
}

void vkglTF::Model::drawNode(Node *node, VkCommandBuffer commandBuffer, uint32_t renderFlags)
{
	if (node->mesh) {
		for (Primitive* primitive : node->mesh->primitives) {
			bool skip = false;
			const vkglTF::Material& material = primitive->material;
			if (renderFlags & RenderFlags::RenderOpaqueNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_OPAQUE);
			}
			if (renderFlags & RenderFlags::RenderAlphaMaskedNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_MASK);
			}
			if (renderFlags & RenderFlags::RenderAlphaBlendedNodes) {
				skip = (material.alphaMode != Material::ALPHAMODE_BLEND);
			}
			if (!skip) {
				if (renderFlags & RenderFlags::BindImages) {
					VkDescriptorSet sets[] = { material.descriptorSet->GetDescriptorSet()};
					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
						targetPipeline->GetPipelineLayout(), material.descriptorSet->GetSetIndex(), 1, sets, 0, nullptr);
				}

				VkDescriptorSet sets[] = { node->mesh->uniformBufferDesc->GetDescriptorSet()};
				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, 
					targetPipeline->GetPipelineLayout(), node->mesh->uniformBufferDesc->GetSetIndex(), 1, sets, 0, nullptr);
				vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, primitive->firstIndex, 0, 0);
			}
		}
	}
	for (auto& child : node->children) {
		drawNode(child, commandBuffer, renderFlags);
	}
}

void vkglTF::Model::draw(VkCommandBuffer commandBuffer, uint32_t renderFlags)
{
	if (!buffersBound) {
		const VkDeviceSize offsets[1] = {0};

		VkBuffer vbs[] = { vertices.buffer->GetBuffer() };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vbs, offsets);
		vkCmdBindIndexBuffer(commandBuffer, indices.buffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
	}
	for (auto& node : nodes) {
		drawNode(node, commandBuffer, renderFlags);
	}
}

void vkglTF::Model::getNodeDimensions(Node *node, glm::vec3 &min, glm::vec3 &max)
{
	if (node->mesh) {
		for (Primitive *primitive : node->mesh->primitives) {
			glm::vec4 locMin = glm::vec4(primitive->dimensions.min, 1.0f) * node->getMatrix();
			glm::vec4 locMax = glm::vec4(primitive->dimensions.max, 1.0f) * node->getMatrix();
			if (locMin.x < min.x) { min.x = locMin.x; }
			if (locMin.y < min.y) { min.y = locMin.y; }
			if (locMin.z < min.z) { min.z = locMin.z; }
			if (locMax.x > max.x) { max.x = locMax.x; }
			if (locMax.y > max.y) { max.y = locMax.y; }
			if (locMax.z > max.z) { max.z = locMax.z; }
		}
	}
	for (auto child : node->children) {
		getNodeDimensions(child, min, max);
	}
}

void vkglTF::Model::getSceneDimensions()
{
	dimensions.min = glm::vec3(FLT_MAX);
	dimensions.max = glm::vec3(-FLT_MAX);
	for (auto node : nodes) {
		getNodeDimensions(node, dimensions.min, dimensions.max);
	}
	dimensions.size = dimensions.max - dimensions.min;
	dimensions.center = (dimensions.min + dimensions.max) / 2.0f;
	dimensions.radius = glm::distance(dimensions.min, dimensions.max) / 2.0f;
}

void vkglTF::Model::updateAnimation(uint32_t index, float time)
{
	if (index > static_cast<uint32_t>(animations.size()) - 1) {
		std::cout << "No animation with index " << index << std::endl;
		return;
	}
	Animation &animation = animations[index];

	bool updated = false;
	for (auto& channel : animation.channels) {
		vkglTF::AnimationSampler &sampler = animation.samplers[channel.samplerIndex];
		if (sampler.inputs.size() > sampler.outputsVec4.size()) {
			continue;
		}

		for (auto i = 0; i < sampler.inputs.size() - 1; i++) {
			if ((time >= sampler.inputs[i]) && (time <= sampler.inputs[i + 1])) {
				float u = std::max(0.0f, time - sampler.inputs[i]) / (sampler.inputs[i + 1] - sampler.inputs[i]);
				if (u <= 1.0f) {
					switch (channel.path) {
					case vkglTF::AnimationChannel::PathType::TRANSLATION: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->translation = glm::vec3(trans);
						break;
					}
					case vkglTF::AnimationChannel::PathType::SCALE: {
						glm::vec4 trans = glm::mix(sampler.outputsVec4[i], sampler.outputsVec4[i + 1], u);
						channel.node->scale = glm::vec3(trans);
						break;
					}
					case vkglTF::AnimationChannel::PathType::ROTATION: {
						glm::quat q1;
						q1.x = sampler.outputsVec4[i].x;
						q1.y = sampler.outputsVec4[i].y;
						q1.z = sampler.outputsVec4[i].z;
						q1.w = sampler.outputsVec4[i].w;
						glm::quat q2;
						q2.x = sampler.outputsVec4[i + 1].x;
						q2.y = sampler.outputsVec4[i + 1].y;
						q2.z = sampler.outputsVec4[i + 1].z;
						q2.w = sampler.outputsVec4[i + 1].w;
						channel.node->rotation = glm::normalize(glm::slerp(q1, q2, u));
						break;
					}
					}
					updated = true;
				}
			}
		}
	}
	if (updated) {
		for (auto &node : nodes) {
			node->update();
		}
	}
}

void vkglTF::Model::createDescriptorsForPipeline(gvk::ptr<gvk::DescriptorAllocator> alloc,gvk::ptr<gvk::Pipeline> targetPipeline)
{
	vkrg_assert(targetPipeline->GetPipelineBindPoint() == VK_PIPELINE_BIND_POINT_GRAPHICS);

	auto objectLayout = targetPipeline->GetInternalLayout(vkglTF::perObjectBindingIndex, (VkShaderStageFlagBits)0);
	auto materialLayout = targetPipeline->GetInternalLayout(vkglTF::perMaterialBindingIndex, (VkShaderStageFlagBits)0);

	createDescriptorSetForAllMaterials(alloc, materialLayout.value());
	createDescriptorSetForAllNodes(alloc, objectLayout.value());

	this->targetPipeline = targetPipeline;
}

void vkglTF::Model::createDescriptorSetForAllMaterials(gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout)
{
	for (auto& material : materials) 
	{
		material.createDescriptorSet(ctx, alloc, layout, &emptyTexture);
	}
}

void vkglTF::Model::createDescriptorSetForAllNodes(gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout)
{
	for (auto& node : nodes)
	{
		if (node->mesh)
		{
			node->mesh->createMeshObjectUBO(alloc, layout);
		}
	}
}

/*
	Helper functions
*/
vkglTF::Node* vkglTF::Model::findNode(Node *parent, uint32_t index) {
	Node* nodeFound = nullptr;
	if (parent->index == index) {
		return parent;
	}
	for (auto& child : parent->children) {
		nodeFound = findNode(child, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

vkglTF::Node* vkglTF::Model::nodeFromIndex(uint32_t index) {
	Node* nodeFound = nullptr;
	for (auto &node : nodes) {
		nodeFound = findNode(node, index);
		if (nodeFound) {
			break;
		}
	}
	return nodeFound;
}

/*
void vkglTF::Model::prepareNodeDescriptor(vkglTF::Node* node, VkDescriptorSetLayout descriptorSetLayout) {
	if (node->mesh) {
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayout;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device->logicalDevice, &descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));

		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = node->mesh->uniformBuffer.descriptorSet;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.pBufferInfo = &node->mesh->uniformBuffer.descriptor;

		vkUpdateDescriptorSets(device->logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
	for (auto& child : node->children) {
		prepareNodeDescriptor(child, descriptorSetLayout);
	}
}
*/

void vkglTF::Material::createDescriptorSet(gvk::ptr<gvk::Context> ctx, gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout, vkglTF::Texture* defaultImage)
{
	gvk::ptr<gvk::DescriptorSet> set = alloc->Allocate(layout).value();

	GvkDescriptorSetWrite update;

	TryUpdateTexture(update, layout, MaterialTexture::Color, set, defaultImage);
	TryUpdateTexture(update, layout, MaterialTexture::Normal, set, defaultImage);
	TryUpdateTexture(update, layout, MaterialTexture::Metallic, set, defaultImage);
	TryUpdateTexture(update, layout, MaterialTexture::Occulsion, set, defaultImage);
	TryUpdateTexture(update, layout, MaterialTexture::Emissive, set, defaultImage);
	TryUpdateTexture(update, layout, MaterialTexture::Glossiness, set, defaultImage);
	TryUpdateTexture(update, layout, MaterialTexture::Diffuse, set, defaultImage);

	update.Emit(ctx->GetDevice());

	descriptorSet = set;
}

/*
void vkglTF::Material::WriteDescriptor(GvkDescriptorSetWrite& update, gvk::ptr<gvk::DescriptorSet> set, MaterialTexture textureType, const char* name)
{
	Texture* texture = GetTexture(textureType);
	vkrg_assert(texture != NULL);
	update.ImageWrite(set, name, texture->sampler, texture->mainView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
*/

void vkglTF::Material::WriteDescriptor(GvkDescriptorSetWrite& update, gvk::ptr<gvk::DescriptorSet> set, MaterialTexture textureType, uint32_t binding)
{
	Texture* texture = GetTexture(textureType);
	vkrg_assert(texture != NULL);
	update.ImageWrite(set, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, binding, texture->sampler, texture->mainView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}



void vkglTF::Material::TryUpdateTexture(GvkDescriptorSetWrite& update, gvk::ptr<gvk::DescriptorSetLayout> layout, MaterialTexture tex, gvk::ptr<gvk::DescriptorSet> set, vkglTF::Texture* defaultImage)
{
	vkglTF::Texture* targetTexture = GetTexture(tex);

	if (targetTexture == nullptr)
	{
		targetTexture = defaultImage;
	}

	std::vector<std::string> keyWordList;
	switch (tex)
	{
	case MaterialTexture::Color:

		keyWordList.push_back("Color");
		keyWordList.push_back("color");
		keyWordList.push_back("Albedo");
		keyWordList.push_back("albedo");
		keyWordList.push_back("Diffuse");
		keyWordList.push_back("diffuse");
		break;
	case MaterialTexture::Normal:
		keyWordList.push_back("Normal");
		keyWordList.push_back("normal");
		keyWordList.push_back("Bump");
		keyWordList.push_back("bump");
		break;
	case MaterialTexture::Metallic:
		keyWordList.push_back("metallic");
		keyWordList.push_back("Metallic");
		keyWordList.push_back("roughness");
		keyWordList.push_back("Roughness");
		break;
	case MaterialTexture::Occulsion:
		keyWordList.push_back("occulsion");
		keyWordList.push_back("Occulsion");
		break;
	case MaterialTexture::Emissive:
		keyWordList.push_back("emissive");
		keyWordList.push_back("Emissive");
		break;
	case MaterialTexture::Glossiness:
		keyWordList.push_back("glossiness");
		keyWordList.push_back("Glossiness");
		break;
	};

	SpvReflectDescriptorBinding* binding = fliterDescriptorSetLayoutBinding(layout, keyWordList);
	if (binding == NULL) return;

	update.ImageWrite(set, (VkDescriptorType)binding->descriptor_type, binding->binding, targetTexture->sampler, targetTexture->mainView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

vkglTF::Texture* vkglTF::Material::GetTexture(MaterialTexture texture)
{
	switch (texture)
	{
	case MaterialTexture::Color:
		return baseColorTexture;
	case MaterialTexture::Metallic:
		return metallicRoughnessTexture;
	case MaterialTexture::Normal:
		return normalTexture;
	case MaterialTexture::Occulsion:
		return occlusionTexture;
	case MaterialTexture::Emissive:
		return emissiveTexture;
	case MaterialTexture::Glossiness:
		return specularGlossinessTexture;
	case MaterialTexture::Diffuse:
		return diffuseTexture;
	}

	return nullptr;
}
