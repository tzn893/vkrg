#version 450
#include "camera.glsli"

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inTangent;

layout (perNode,binding = 0) uniform ObjectUBO
{
	Object object;
} objectUBO;

layout (perCamera,binding = 0) uniform CameraUBO 
{
	Camera camera;
} cameraUBO;


void main() 
{
	gl_Position = cameraUBO.camera.projection * cameraUBO.camera.view * objectUBO.object.model * vec4(inPos, 1);
}
