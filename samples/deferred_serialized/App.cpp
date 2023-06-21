#include "DeferredPass.h"
#include <vkrg/graph.h>

int main()
{
    ptr<RenderGraph> graph = std::make_shared<RenderGraph>();
    ptr<RenderPass> deferred = RenderPassFactory::CreateRenderPass("DeferredPass", "pass1").value();


    deferred->Execute();
}