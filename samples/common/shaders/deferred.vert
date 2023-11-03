#version 450

layout (location = 0) out vec2 outUV;


vec2 uv[6] = 
{
	{ 0,  0},
	{ 0,  1},
	{ 1,  1},
	{ 0,  0},
	{ 1,  1},
	{ 1,  0}
};

void main() 
{
	outUV = uv[gl_VertexIndex];
	gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}
