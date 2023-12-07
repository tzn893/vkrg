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

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec2 outUV;
layout (location = 2) out vec3 outWorldPos;
layout (location = 3) out vec3 outTangent;

void main() 
{
	vec4 tmpPos = vec4(inPos, 1);
	gl_Position = cameraUBO.camera.projection * cameraUBO.camera.view * objectUBO.object.model * tmpPos;
	
	outUV = inUV;

	// Vertex position in world space
	outWorldPos = vec3(objectUBO.object.model * tmpPos);
	
	// Normal in world space
	mat3 mNormal = mat3(objectUBO.object.invTransModel);
	outNormal = normalize(mNormal * inNormal);	
	outTangent = mNormal * normalize(inTangent);
}
