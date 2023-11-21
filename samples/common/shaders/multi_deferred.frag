#version 450
#include "camera.glsli"
#include "light.glsli"
#include "pbr.glsli"

layout (location = 0) in vec2 inUV;
layout (perCamera, binding = 0) uniform CameraUBO
{
	Camera camera;
} cameraUBO;

layout (perDraw, binding = 0) uniform LightUBO
{
	Light lights[2047];
    int   count;
} lightUBO;

layout (perMaterial, binding = 0) uniform sampler2D colorSampler;
layout (perMaterial, binding = 1) uniform sampler2D materialSampler;
layout (perMaterial, binding = 2) uniform sampler2D normalSampler;
layout (perMaterial, binding = 3) uniform sampler2D depthSampler;

layout (location = 0) out vec4 outColor;



PBRParameter GetPbrParameter()
{
	vec4 normal = texture(normalSampler, inUV);
	vec2 metallicRoughness = texture(materialSampler, inUV).xy;
	vec3 color = texture(colorSampler, inUV).xyz;

	float depth = texture(depthSampler, inUV).x;

	vec3 ndc = vec3(inUV * 2 - 1, depth);

    // 将NDC空间坐标转换到剪裁空间
    vec4 clip = vec4(ndc, 1.0);
    // 使用逆投影矩阵将剪裁空间坐标转换到观察空间
    vec4 view = cameraUBO.camera.invProjection * clip;
    // 将齐次坐标转换到笛卡尔坐标
	vec4 position = (cameraUBO.camera.invView * view);
	position /= position.w;


	PBRParameter param;
	param.N = normalize(normal.xyz * 2.0 - 1.0);

	
	param.V = normalize(cameraUBO.camera.cameraPosition - position.xyz);
	param.albedo = color;
	param.roughness = metallicRoughness.y;
	param.metallic = 0;//metallicRoughness.x;
	param.P = position.xyz;

	return param;
}


void main()
{		  
	
	PBRParameter param = GetPbrParameter();
	// Specular contribution
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < lightUBO.count; i++) {
		Lo += ComputePBR(param ,lightUBO.lights[i]);
	};

	// Combine with ambient
	vec3 color = param.albedo * 0.2;
	color += Lo;

	// Gamma correct
	// color = pow(color, vec3(0.4545));

	outColor = vec4(color, 1.0);
}