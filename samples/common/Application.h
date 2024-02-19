#pragma once
#include <vkrg/graph.h>
#include "Camera.h"
using namespace vkrg;


class Application
{
public:
	Application(uint32_t width, uint32_t height, bool async, GVK_DEVICE_EXTENSION* extraExtensions = NULL, uint32_t extraExtensionCnt = 0,
		GVK_INSTANCE_EXTENSION* extraInstanceExts = NULL, uint32_t extraInstanceExtensionCnt = 0);

	void Run();

protected:

	bool InitializeContext();
	virtual bool CustomInitialize() { return true; };

	virtual void CreateRenderGraph() = 0;

	virtual void MainLoop(VkCommandBuffer cmd, uint32_t imageIdx) {};
	virtual void CameraMoveEvent() { }

	ptr<gvk::CommandQueue> queue;
	VkFence				   fences[4] = { NULL };
	VkSemaphore			   color_output_finish_semaphore[4] = { NULL };

	gvk::ptr<gvk::DescriptorAllocator> descriptorAlloc;
	ptr<gvk::Window> window;
	ptr<gvk::Context> context;
	ptr<RenderGraph> rg;
	uint32_t		 backBufferCount;

	uint32_t windowWidth = 800;
	uint32_t windowHeight = 800;
	bool async;

	ptr<gvk::CommandPool> cmdPool;
	VkCommandBuffer		  cmdBuffer[4] = { NULL };

	Camera m_Camera;

	float yaw = 0.0f;// yaw is initialized to -90.0 degrees since a yaw of 0.0 results in a direction vector pointing to the right so we initially rotate a bit to the left.
	float pitch = 0.0f;
	float fov = 45.0f;

	glm::vec3 cameraPos = glm::vec3(0.0f, 4.0f, 0.0f);
	glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, 1.0f);

	float cameraSpeed = 5.0f;
	float sensitive = 30.0f;
	
	VkFormat m_BackBufferFormat;

	std::vector<GVK_DEVICE_EXTENSION> m_DeviceExtensions;
	std::vector<GVK_INSTANCE_EXTENSION> m_InstanceExtensions;
};