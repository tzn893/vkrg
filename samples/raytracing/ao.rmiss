#version 460
#extension GL_EXT_ray_tracing : require
// #extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "ao_common.glsli"

layout(location = 0) rayPayloadInEXT hitPayload prd;

void main()
{
  prd.hit = 1;
}