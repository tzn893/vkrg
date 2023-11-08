#version 450
#include "tile.glsli"

layout (location = 0) in vec2 inUV;
layout (perCamera, binding = 0) uniform CameraUBO
{
	Camera camera;
} cameraUBO;

layout(perDraw, binding = 0) buffer CullingResult
{
    // currently we assume maximun screen resolution is 1920x1080
    TileLighting tileLighting[8100];
} cullingResult;

layout(perDraw, binding = 1) uniform ScreenUBO
{
    Screen ubo;
} screen;

layout (perMaterial, binding = 0) uniform sampler2D colorSampler;
layout (perMaterial, binding = 1) uniform sampler2D materialSampler;
layout (perMaterial, binding = 2) uniform sampler2D normalSampler;
layout (perMaterial, binding = 3) uniform sampler2D depthSampler;

layout (location = 0) out vec4 outColor;

const float PI = 3.14159265359;


//#define ROUGHNESS_PATTERN 1
struct PBRParameter 
{
	vec3  P;
	vec3  N;
	vec3  V;
	vec3  albedo;
	float roughness;
	float metallic;
};

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
	param.metallic = metallicRoughness.x;
	param.P = position.xyz;

	return param;
}

vec3 GetL(Light light, vec3 position, out vec3 intensity)
{
	int lightType = int(light.vec.w);
	if(lightType == LIGHT_TYPE_DIRECT)
	{
		intensity = light.intensity.xyz;
		return -normalize(light.vec.xyz);
	}
	else
	{
		vec3 diff = light.vec.xyz - position;
		intensity = light.intensity.xyz / (1 + dot(diff, diff));
		return normalize(diff);
	}
}

// Normal Distribution function --------------------------------------
float D_GGX(float dotNH, float roughness)
{
	float alpha = roughness * roughness;
	float alpha2 = alpha * alpha;
	float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
	return (alpha2)/(PI * denom*denom); 
}

// Geometric Shadowing function --------------------------------------
float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r*r) / 8.0;
	float GL = dotNL / (dotNL * (1.0 - k) + k);
	float GV = dotNV / (dotNV * (1.0 - k) + k);
	return GL * GV;
}

// Fresnel function ----------------------------------------------------
vec3 F_Schlick(float cosTheta, float metallic, vec3 albedo)
{
	vec3 F0 = mix(vec3(0.04), albedo, metallic); // * material.specular
	vec3 F = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0); 
	return F;    
}

// Specular BRDF composition --------------------------------------------

vec3 BRDF(vec3 L, vec3 V, vec3 N, float metallic, float roughness, vec3 albedo)
{
	// Precalculate vectors and dot products	
	vec3 H = normalize (V + L);
	float dotNV = clamp(dot(N, V), 0.001, 1.0);
	float dotNL = clamp(dot(N, L), 0.000, 1.0);
	float dotLH = clamp(dot(L, H), 0.001, 1.0);
	float dotNH = clamp(dot(N, H), 0.001, 1.0);

	vec3 color = vec3(0.0);

	if (dotNL > 0.0)
	{
		float rroughness = max(0.05, roughness);
		// D = Normal distribution (Distribution of the microfacets)
		float D = D_GGX(dotNH, roughness); 
		// G = Geometric shadowing term (Microfacets shadowing)
		float G = G_SchlicksmithGGX(dotNL, dotNV, rroughness);
		// F = Fresnel factor (Reflectance depending on angle of incidence)
		vec3 F = F_Schlick(dotNV, metallic, albedo);

		vec3 Ks = F;
		vec3 Kd = (1 - Ks) * (1 - metallic);

		vec3 spec = D * F * G / (4.0 * dotNL * dotNV);

		color += (albedo * Kd / PI + spec) * dotNL;
	}

	return color;
}


vec3 ComputePBR(PBRParameter param, Light light)
{
	vec3 L, Li;
	L = GetL(light, param.P, Li);

	vec3 Lo = BRDF(L, param.V, param.N, param.metallic, param.roughness, param.albedo) * Li;

	return Lo;
}
// ----------------------------------------------------------------------------
void main()
{		  
	TileLighting lights = cullingResult.tileLighting[GetTileIdx(screen.ubo, inUV)];

	PBRParameter param = GetPbrParameter();
	// Specular contribution
	vec3 Lo = vec3(0.0);
	for (int i = 0; i < lights.lightCount.x; i++) {
		Lo += ComputePBR(param ,lights.lights[i]);
	};

	// Combine with ambient
	vec3 color = param.albedo * 0.02;
	color += Lo;

	// Gamma correct
	// color = pow(color, vec3(0.4545));

	outColor = vec4(color, 1.0);
}