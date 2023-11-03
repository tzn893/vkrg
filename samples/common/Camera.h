#pragma once
#include <vkrg/common.h>
#include <glm/glm.hpp>
#include <mutex>

struct CameraUBO
{
	glm::mat4 invProjection;
	glm::mat4 projection;
	glm::mat4 invView;
	glm::mat4 view;
	glm::vec3 cameraPosition;
};

class Camera 
{
	friend class World;
public:
	Camera(uint32_t width, uint32_t height,glm::vec3 pos, glm::vec3 front);
	
	glm::vec3		GetPosition();
	glm::vec3		GetFront();

	void			SetPosition(glm::vec3 pos);
	void			SetFront(glm::vec3 front);

	float			GetFar();
	float			GetNear();
	float			GetFov();
	
	void			SetFar(float far);
	void			SetNear(float near);
	void			SetFov(float fov);

	CameraUBO		GetCameraUBO();
	void			OnResize(uint32_t win_width, uint32_t win_height);

protected:
	float m_Far,m_Near,m_AspectRatio,m_Fov;

	glm::vec3 m_Position;
	glm::vec3 m_Front;
};
