#version 450
#include "tile.glsli"
#include "light.glsli"
#include "pbr.glsli"

layout (perCamera, binding = 0) uniform CameraUBO
{
	Camera camera;
} cameraUBO;

layout(perDraw, binding = 1) buffer CullLightGrid
{
    LightGrid grid;
} lightGrid;

layout(perDraw, binding = 2) buffer LightIndexUBO
{
    LightIndexBuffer indices;
} lightIndex;

layout(perDraw, binding = 3) uniform LightUBO
{
	Light lights[2047];
    int   count;
} lightUBO;

layout(perDraw, binding = 0) uniform ScreenUBO
{
    Screen ubo;
} screenUBO;

layout (perMaterial, binding = 0) uniform sampler2D samplerMetallicRoughness;
layout (perMaterial, binding = 1) uniform sampler2D samplerColor;
layout (perMaterial, binding = 2) uniform sampler2D samplerNormalMap;

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inWorldPos;
layout (location = 3) in vec3 inTangent;

layout (location = 0) out vec4 outColor;


PBRParameter GetPbrParameter()
{
    vec3 N = normalize(inNormal);
	vec3 T = normalize(inTangent);
	vec3 B = cross(N, T);
	mat3 TBN = mat3(T, B, N);
	vec3 tnorm = TBN * normalize(texture(samplerNormalMap, inUV).xyz * 2.0 - vec3(1.0));
	
	vec2 metallicRoughness = texture(samplerMetallicRoughness, inUV).xy;
	vec3 color = texture(samplerColor, inUV).xyz;

	PBRParameter param;
	param.N = tnorm;

	param.V = normalize(cameraUBO.camera.cameraPosition - inWorldPos);
	param.albedo = color;
	param.roughness = metallicRoughness.y;
	param.metallic = metallicRoughness.x;
	param.P = inWorldPos;

	return param;
}


// ----------------------------------------------------------------------------
void main()
{		  
	int tileIdx = GetTileIdx(screenUBO.ubo, gl_FragCoord.xy * screenUBO.ubo.resolution.zw);

	PBRParameter param = GetPbrParameter();
	// Specular contribution
	vec3 Lo = vec3(0.0);
	uint lightCount = lightGrid.grid.grid[tileIdx].y;
	uint lightOffset = lightGrid.grid.grid[tileIdx].x;
	

	for (uint i = 0; i < lightCount; i++) {
		uint idx = lightIndex.indices.indices[lightOffset + i];
		Lo += ComputePBR(param ,lightUBO.lights[idx]);
	};

	// Combine with ambient
	vec3 color = param.albedo * 0.2;
	color += Lo;

	outColor = vec4(color , 1.0);
}