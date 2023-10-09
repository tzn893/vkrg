#include "DeferredPass.h"
#include "DeferredShading.h"
#include <vkrg/graph.h>
using namespace vkrg;

int main()
{
	ptr<RenderGraph> rg = std::make_shared<RenderGraph>();
	
	rg->CreatePass<DeferredPass>("gbuffer");
	rg->CreatePass<DeferredShading>("deferred-shading");

	rg->AddEdge("gbuffer", "gbuffer-color", "deferred-shading", "gbuffer-color");
	rg->AddEdge("gbuffer", "gbuffer-normal-depth", "deferred-shading", "gbuffer-normal-depth");
	rg->AddEdge("gbuffer", "gbuffer-material", "deferred-shading", "gbuffer-material");

	ResourceSlice slice;
	slice.arr_cnt = 1;
	slice.arr_idx = 0;
	slice.mip_cnt = 1;
	slice.mip_idx = 0;
	// depth-buffer and back-buffer are added when graph is created so we don't need to add them to render graph
	rg->AddResourceOutput("gbuffer", "depth-buffer", "depth-buffer", slice);
	rg->AddResourceOutput("deferred-shading", "color", "back-buffer", slice);

	rg->Compile();

	rg->StoreToJson("graph.json");
}