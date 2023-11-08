/*
* Vulkan glTF model and texture loading class based on tinyglTF (https://github.com/syoyo/tinygltf)
*
* Copyright (C) 2018 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#include <stdlib.h>
#include <string>
#include <fstream>
#include <vector>

#include <gvk.h>
#include <vkrg/common.h>

#include <ktx.h>
#include <ktxvulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYGLTF_NO_STB_IMAGE_WRITE
#ifdef VK_USE_PLATFORM_ANDROID_KHR
#define TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#endif
#include "tiny_gltf.h"


#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif




namespace vkglTF
{
	struct BoundingBox
	{
		glm::vec3 upper;
		glm::vec3 lower;

		BoundingBox() :upper(std::numeric_limits<float>::min(), std::numeric_limits<float>::min(), std::numeric_limits<float>::min()),
			lower(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()) {}

		void Merge(glm::vec3 pt);
	};


	inline constexpr uint32_t perObjectBindingIndex = 0;
	inline constexpr uint32_t perMaterialBindingIndex = 1;
	inline constexpr uint32_t perCameraBindingIndex = 2;
	inline constexpr uint32_t perDrawBindingIndex = 3;

	enum DescriptorBindingFlags {
		ImageBaseColor = 0x00000001,
		ImageNormalMap = 0x00000002
	};

	struct Node;

	/*
		glTF texture loading class
	*/
	struct Texture {
		
		gvk::ptr<gvk::Image>	image;
		VkSampler sampler;
		VkImageView mainView;

		void destroy();
		void fromglTfImage(tinygltf::Image& gltfimage, std::string path, gvk::ptr<gvk::Context> device);
	};

	/*
		glTF material class
	*/
	enum class MaterialTexture
	{
		Color,
		Normal,
		Metallic,
		Occulsion,
		Emissive,
		Glossiness,
		Diffuse
	};


	struct Material {
		enum AlphaMode { ALPHAMODE_OPAQUE, ALPHAMODE_MASK, ALPHAMODE_BLEND };
		AlphaMode alphaMode = ALPHAMODE_OPAQUE;
		float alphaCutoff = 1.0f;
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		glm::vec4 baseColorFactor = glm::vec4(1.0f);
		vkglTF::Texture* baseColorTexture = nullptr;
		vkglTF::Texture* metallicRoughnessTexture = nullptr;
		vkglTF::Texture* normalTexture = nullptr;
		vkglTF::Texture* occlusionTexture = nullptr;
		vkglTF::Texture* emissiveTexture = nullptr;

		vkglTF::Texture* specularGlossinessTexture;
		vkglTF::Texture* diffuseTexture;

		gvk::ptr<gvk::DescriptorSet> descriptorSet = VK_NULL_HANDLE;

		// Material(vks::VulkanDevice* device) : device(device) {};
		// void createDescriptorSet(VkDescriptorPool descriptorPool, VkDescriptorSetLayout descriptorSetLayout, uint32_t descriptorBindingFlags);

		void createDescriptorSet(gvk::ptr<gvk::Context> ctx,gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout, vkglTF::Texture* defaultImage);
		//void WriteDescriptor(GvkDescriptorSetWrite& update, gvk::ptr<gvk::DescriptorSet> set, MaterialTexture texture, const char* name);
		void WriteDescriptor(GvkDescriptorSetWrite& update, gvk::ptr<gvk::DescriptorSet> set, MaterialTexture texture, uint32_t binding);

	private:

		void TryUpdateTexture(GvkDescriptorSetWrite& update, gvk::ptr<gvk::DescriptorSetLayout> layout,MaterialTexture tex, gvk::ptr<gvk::DescriptorSet> set, vkglTF::Texture* defaultImage);

		vkglTF::Texture* GetTexture(MaterialTexture texture);
	};

	/*
		glTF primitive
	*/
	struct Primitive {
		uint32_t firstIndex;
		uint32_t indexCount;
		uint32_t firstVertex;
		uint32_t vertexCount;
		Material& material;

		struct Dimensions {
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
			glm::vec3 size;
			glm::vec3 center;
			float radius;
		} dimensions;

		void setDimensions(glm::vec3 min, glm::vec3 max);
		Primitive(uint32_t firstIndex, uint32_t indexCount, Material& material) : firstIndex(firstIndex), indexCount(indexCount), material(material) {};
	};

	/*
		glTF mesh
	*/
	struct Mesh {
		gvk::ptr<gvk::Context>  ctx;
		std::vector<Primitive*> primitives;
		std::string name;

		gvk::ptr<gvk::Buffer> uniformBuffer;
		gvk::ptr<gvk::DescriptorSet> uniformBufferDesc;
		gvk::ptr<gvk::DescriptorSetLayout> targetLayout = nullptr;

		struct ObjectUBO
		{
			glm::mat4 model;
			glm::mat4 invTransModel;
		} ubo;

		void UpdateUBO(glm::mat4 m);
		void createMeshObjectUBO(gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout);

		Mesh(gvk::ptr<gvk::Context> ctx, glm::mat4 model);
		~Mesh();
	};

	/*
		glTF skin
	*/
	struct Skin {
		std::string name;
		Node* skeletonRoot = nullptr;
		std::vector<glm::mat4> inverseBindMatrices;
		std::vector<Node*> joints;
	};

	/*
		glTF node
	*/
	struct Node {
		Node* parent;
		uint32_t index;
		std::vector<Node*> children;
		glm::mat4 matrix;
		std::string name;
		Mesh* mesh;
		Skin* skin;
		int32_t skinIndex = -1;
		glm::vec3 translation{};
		glm::vec3 scale{ 1.0f };
		glm::quat rotation{};
		glm::mat4 localMatrix();
		glm::mat4 getMatrix();
		void update();
		~Node();
	};

	/*
		glTF animation channel
	*/
	struct AnimationChannel {
		enum PathType { TRANSLATION, ROTATION, SCALE };
		PathType path;
		Node* node;
		uint32_t samplerIndex;
	};

	/*
		glTF animation sampler
	*/
	struct AnimationSampler {
		enum InterpolationType { LINEAR, STEP, CUBICSPLINE };
		InterpolationType interpolation;
		std::vector<float> inputs;
		std::vector<glm::vec4> outputsVec4;
	};

	/*
		glTF animation
	*/
	struct Animation {
		std::string name;
		std::vector<AnimationSampler> samplers;
		std::vector<AnimationChannel> channels;
		float start = std::numeric_limits<float>::max();
		float end = std::numeric_limits<float>::min();
	};

	/*
		glTF default vertex layout with easy Vulkan mapping functions
	*/
	enum class VertexComponent { Position, Normal, UV, Color, Tangent, Joint0, Weight0, MaxEnum };

	struct Vertex {
		glm::vec3 pos;
		glm::vec3 normal;
		glm::vec2 uv;
		glm::vec4 color;
		glm::vec4 joint0;
		glm::vec4 weight0;
		glm::vec4 tangent;
		static VkVertexInputBindingDescription vertexInputBindingDescription;
		static std::vector<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions;
		static VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo;
		static VkVertexInputBindingDescription inputBindingDescription(uint32_t binding);
		static VkVertexInputAttributeDescription inputAttributeDescription(uint32_t binding, uint32_t location, VertexComponent component);
		static std::vector<VkVertexInputAttributeDescription> inputAttributeDescriptions(uint32_t binding, const std::vector<VertexComponent> components);
		/** @brief Returns the default pipeline vertex input state create info structure for the requested vertex components */
		static VkPipelineVertexInputStateCreateInfo* getPipelineVertexInputState(const std::vector<VertexComponent> components);
	};

	enum FileLoadingFlags {
		None = 0x00000000,
		PreTransformVertices = 0x00000001,
		PreMultiplyVertexColors = 0x00000002,
		FlipY = 0x00000004,
		DontLoadImages = 0x00000008
	};

	enum RenderFlags {
		BindImages = 0x00000001,
		RenderOpaqueNodes = 0x00000002,
		RenderAlphaMaskedNodes = 0x00000004,
		RenderAlphaBlendedNodes = 0x00000008
	};

	/*
		glTF model loading and rendering class
	*/
	class Model {
	private:
		vkglTF::Texture* getTexture(uint32_t index);
		vkglTF::Texture emptyTexture;
		void createEmptyTexture(gvk::ptr<gvk::Context> ctx,gvk::ptr<gvk::CommandQueue> queue);

		gvk::ptr<gvk::Pipeline> targetPipeline;
		BoundingBox boundBox;

		void createDescriptorSetForAllMaterials(gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout);
		void createDescriptorSetForAllNodes(gvk::ptr<gvk::DescriptorAllocator> alloc, gvk::ptr<gvk::DescriptorSetLayout> layout);

	public:
		gvk::ptr<gvk::Context> ctx;

		struct Vertices {
			int count;
			gvk::ptr<gvk::Buffer> buffer;
		} vertices;
		struct Indices {
			int count;
			gvk::ptr<gvk::Buffer> buffer;
		} indices;

		std::vector<Node*> nodes;
		std::vector<Node*> linearNodes;

		std::vector<Skin*> skins;

		std::vector<Texture> textures;
		std::vector<Material> materials;
		std::vector<Animation> animations;

		struct Dimensions {
			glm::vec3 min = glm::vec3(FLT_MAX);
			glm::vec3 max = glm::vec3(-FLT_MAX);
			glm::vec3 size;
			glm::vec3 center;
			float radius;
		} dimensions;

		bool metallicRoughnessWorkflow = true;
		bool buffersBound = false;
		std::string path;

		Model() {};
		~Model();
		void loadNode(vkglTF::Node* parent, const tinygltf::Node& node, uint32_t nodeIndex, const tinygltf::Model& model, std::vector<uint32_t>& indexBuffer, std::vector<Vertex>& vertexBuffer, float globalscale);
		void loadSkins(tinygltf::Model& gltfModel);
		void loadImages(tinygltf::Model& gltfModel, gvk::ptr<gvk::Context> device);
		void loadMaterials(tinygltf::Model& gltfModel);
		void loadAnimations(tinygltf::Model& gltfModel);
		void loadFromFile(std::string filename , gvk::ptr<gvk::Context> device, SpvReflectInterfaceVariable** variables, uint32_t variableCount, uint32_t fileLoadingFlags = vkglTF::FileLoadingFlags::None, float scale = 1.0f);
		void bindBuffers(VkCommandBuffer commandBuffer);
		void drawNode(Node* node, VkCommandBuffer commandBuffer, uint32_t renderFlags = 0);
		void draw(VkCommandBuffer commandBuffer, uint32_t renderFlags = 0);
		void getNodeDimensions(Node* node, glm::vec3& min, glm::vec3& max);
		void getSceneDimensions();
		void updateAnimation(uint32_t index, float time);

		void createDescriptorsForPipeline(gvk::ptr<gvk::DescriptorAllocator> alloc,gvk::ptr<gvk::Pipeline> targetPipeline);
		
		Node* findNode(Node* parent, uint32_t index);
		Node* nodeFromIndex(uint32_t index);

		BoundingBox GetBox();
		// void prepareNodeDescriptor(vkglTF::Node* node, VkDescriptorSetLayout descriptorSetLayout);
	};
}