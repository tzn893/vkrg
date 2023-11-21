#version 450
#extension GL_EXT_debug_printf : enable

#include "camera.glsli"

layout (perMaterial, binding = 0) uniform sampler2D samplerMetallicRoughness;
layout (perMaterial, binding = 1) uniform sampler2D samplerColor;
layout (perMaterial, binding = 2) uniform sampler2D samplerNormalMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inWorldPos;
layout (location = 3) in vec3 inTangent;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outMaterial;
layout (location = 2) out vec4 outNormal;

void main() 
{
	// Calculate normal in tangent space
	vec3 N = normalize(inNormal);
	vec3 T = normalize(inTangent);
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);
	vec3 tnorm = TBN * normalize(texture(samplerNormalMap, inUV).xyz * 2.0 - vec3(1.0));
	outNormal = vec4(tnorm * 0.5 + 0.5, 1.0) ;

	outColor = texture(samplerColor, inUV);
	outMaterial.xy = texture(samplerMetallicRoughness, inUV).xy;
}