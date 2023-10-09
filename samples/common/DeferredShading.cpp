#include "DeferredShading.h"

REGISTER_EXECUTABLE_PASS_CREATE(DeferredShading);

DeferredShading::DeferredShading(const std::string& name)
: ExecutablePass(name)
{

}

void DeferredShading::Compile(ExecutablePassPrototypeInfoCollector& collector)
{

}

void DeferredShading::Execute()
{

}

VKRG_RENDER_PASS_TYPE DeferredShading::GetType()
{
	return VKRG_RP_TYPE_RENDER_PASS;
}

