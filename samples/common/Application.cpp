#include "Application.h"

Application::Application(uint32_t width, uint32_t height, bool async)
	:windowWidth(width), windowHeight(height), async(async), m_Camera(width, height, cameraPos, cameraFront)
{

}

void Application::Run()
{
	if (!InitializeContext()) return;
	
	CreateRenderGraph();

	if (!CustomInitialize()) return;

	glm::vec3 cameraUp = glm::vec3(0, 1, 0);

	// main loop
	while (true)
	{

		float delta_time = 0.01;
		float speed = cameraSpeed;
		if (window->KeyHold(GVK_KEY_SHIFT))
		{
			speed *= 5.f;
		}

		//update main camera
		if (window->KeyHold(GVK_KEY_W))
		{
			cameraPos -= speed * delta_time * cameraFront;
		}
		if (window->KeyHold(GVK_KEY_S))
		{
			cameraPos += speed * delta_time * cameraFront;
		}
		if (window->KeyHold(GVK_KEY_A))
		{
			cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * speed * delta_time;
		}
		if (window->KeyHold(GVK_KEY_D))
		{
			cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * speed * delta_time;
		}

		if (window->KeyHold(GVK_MOUSE_1) && window->MouseMove())
		{
			GvkVector2 offset = window->GetMouseOffset();
			yaw += offset.x * sensitive * delta_time;
			pitch -= offset.y * sensitive * delta_time;
			if (pitch > 89.99f) pitch = 89.98f;
			if (pitch < -89.99f) pitch = -89.98f;

			glm::vec3 front;
			front.z = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
			front.y = sin(glm::radians(pitch));
			front.x = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
			cameraFront = glm::normalize(front);
		}


		m_Camera.SetPosition(cameraPos);
		m_Camera.SetFront(cameraFront);

		uint32_t current_frame_idx = context->CurrentFrameIndex();
		uint32_t current_fence_idx = 0;
		uint32_t current_semaphore_idx = 0;
		uint32_t current_command_buffer_idx = 0;
		if (async)
		{
			current_fence_idx = current_frame_idx;
			current_semaphore_idx = current_frame_idx;
			current_command_buffer_idx = current_frame_idx;

			vkWaitForFences(context->GetDevice(), 1, &fences[current_fence_idx], VK_TRUE, 0xffffffff);
			vkResetFences(context->GetDevice(), 1, &fences[current_fence_idx]);
		}


		VkCommandBuffer cmd = cmdBuffer[current_command_buffer_idx];
		vkResetCommandBuffer(cmd, 0);

		VkCommandBufferBeginInfo cmdBeginInfo{};
		cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		cmdBeginInfo.flags = 0;
		vkBeginCommandBuffer(cmd, &cmdBeginInfo);

		std::string error;

		auto on_resize = [&](uint32_t w, uint32_t h)
		{
			windowWidth = w;
			windowHeight = h;
			rg->OnResize(w, h);
			return true;
		};

		uint32_t current_image_idx;
		VkSemaphore acquire_image_semaphore;
		if (auto v = context->AcquireNextImageAfterResize(on_resize, &error))
		{
			auto [_, a, i] = v.value();
			acquire_image_semaphore = a;
			current_image_idx = i;
		}
		else
		{
			printf("%s", error.c_str());
			break;
		}

		MainLoop(cmd, current_image_idx);

		VkSemaphore color_output_finish = color_output_finish_semaphore[current_command_buffer_idx];
		
		vkEndCommandBuffer(cmd);

		queue->Submit(&cmd, 1,
			gvk::SemaphoreInfo()
			.Wait(acquire_image_semaphore, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)
			.Signal(color_output_finish_semaphore[current_command_buffer_idx]), 
		NULL);

		context->Present(gvk::SemaphoreInfo().Wait(color_output_finish, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT));

		if (!async)
		{
			context->WaitForDeviceIdle();
		}

		window->UpdateWindow();
	}
}

bool Application::InitializeContext()
{
	window = gvk::Window::Create(windowWidth, windowHeight, "triangle").value();

	std::string error;
	context = gvk::Context::CreateContext("triangle", GVK_VERSION{ 1,0,0 }, VK_API_VERSION_1_3, window, &error).value();

	GvkInstanceCreateInfo instance_create;
	instance_create.AddInstanceExtension(GVK_INSTANCE_EXTENSION_DEBUG);
	instance_create.AddLayer(GVK_LAYER_DEBUG);
	instance_create.AddLayer(GVK_LAYER_FPS_MONITOR);

	context->InitializeInstance(instance_create, &error);

	GvkDeviceCreateInfo device_create;
	device_create.AddDeviceExtension(GVK_DEVICE_EXTENSION_SWAP_CHAIN);
	device_create.RequireQueue(VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT, 1);

	context->InitializeDevice(device_create, &error);
	
	m_BackBufferFormat = context->PickBackbufferFormatByHint({ VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_UNORM });
	context->CreateSwapChain(m_BackBufferFormat, &error);
	
	backBufferCount = context->GetBackBufferCount();

	queue = context->CreateQueue(VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT).value();

	cmdPool = context->CreateCommandPool(queue.get()).value();
	for (uint32_t i = 0; i < backBufferCount; i++)
	{
		cmdBuffer[i] = cmdPool->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY).value();
		fences[i] = context->CreateFence(VK_FENCE_CREATE_SIGNALED_BIT).value();
		color_output_finish_semaphore[i] = context->CreateVkSemaphore().value();
	}
	descriptorAlloc = context->CreateDescriptorAllocator();

	return true;
}
