#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>

constexpr float pi = 3.14159265;
Camera::Camera(uint32_t width, uint32_t height, glm::vec3 pos, glm::vec3 front) :
	 m_Far(1000.f), m_Near(0.01f), m_Fov(pi * .5f)
{
	OnResize(width,height);
}

glm::vec3 Camera::GetPosition()
{
	return m_Position;
}

glm::vec3 Camera::GetFront()
{
	return m_Front;
}

void Camera::SetPosition(glm::vec3 pos)
{
	m_Position = pos;
}

void Camera::SetFront(glm::vec3 front)
{
	m_Front = front;
}

float Camera::GetFar()
{
	return m_Far;
}

float Camera::GetNear()
{
	return m_Near;
}

void Camera::SetFar(float f)
{
	m_Far = f;
}

void Camera::SetNear(float n)
{
	m_Near = n;
}

void Camera::SetFov(float fov)
{
	m_Fov = fov;
}

CameraUBO Camera::GetCameraUBO()
{
	glm::mat4 proj = glm::perspectiveLH_ZO(m_Fov, m_AspectRatio, m_Near, m_Far);
	glm::mat4 view = glm::lookAt(m_Position, m_Position + m_Front, glm::vec3(0, 1, 0));

	// proj[0][0] *= -1;
	   proj[1][1] *= -1;

	CameraUBO ubo;
	ubo.cameraPosition = m_Position;
	ubo.invProjection = glm::inverse(proj);
	ubo.projection = proj;
	ubo.invView = glm::inverse(view);
	ubo.view = view;

	return ubo;
}

void Camera::OnResize(uint32_t win_width, uint32_t win_height)
{
	m_AspectRatio = (float)win_width / (float)win_height;
}

float Camera::GetFov()
{
	return m_Fov;
}

