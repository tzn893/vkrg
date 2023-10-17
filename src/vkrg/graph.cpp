#include "graph.h"


namespace vkrg
{
    RenderGraph::RenderGraph()
    {
    }

    opt<ResourceHandle> RenderGraph::FindGraphResource(const char* name)
    {
        if (auto iter = m_LogicalResourceTable.find(name);iter != m_LogicalResourceTable.end())
        {
            return m_LogicalResourceList[iter->second].handle;
        }

        return std::nullopt;
    }

    opt<ResourceHandle> RenderGraph::GetGraphResource(uint32_t idx)
    {
        if (idx < m_LogicalResourceList.size()) return m_LogicalResourceList[idx].handle;
        return std::nullopt;
    }

    opt<RenderPassHandle> RenderGraph::FindGraphRenderPass(const char* name)
    {
        if (auto iter = m_RenderPassTable.find(name);iter != m_RenderPassTable.end())
        {
            return m_RenderPassList[iter->second];
        }

        return std::nullopt;
    }

    opt<RenderPassHandle> RenderGraph::GetGraphRenderPass(uint32_t idx)
    {
        if (idx < m_RenderPassList.size()) return m_RenderPassList[idx];
        return std::nullopt;
    }

    opt<ResourceHandle> RenderGraph::AddGraphResource(const char* name, ResourceInfo info, bool external, VkImageLayout layout)
    {
        vkrg_assert(!external || layout == VK_IMAGE_LAYOUT_UNDEFINED);

        if (FindGraphResource(name).has_value()) return std::nullopt;

        ResourceHandle handle;
        handle.idx = m_LogicalResourceList.size();
        handle.external = external;

        LogicalResource resource;
        resource.handle = handle;
        resource.info = info;
        resource.name = name;
        resource.finalLayout = layout;

        m_LogicalResourceList.push_back(resource);
        m_LogicalResourceTable[name] = handle.idx;

        return handle;
    }

    opt<RenderPassHandle> RenderGraph::AddGraphRenderPass(const char* name, RenderPassType type)
    {
        if (FindGraphRenderPass(name).has_value()) return std::nullopt;

        ptr<RenderPass> pass = std::make_shared<RenderPass>(this, name, type);

        RenderPassHandle handle;
        handle.pass = pass;
        handle.idx = m_RenderPassList.size();

        m_RenderPassList.push_back(handle);
        m_RenderPassTable[name] = handle.idx;
        
        return handle;
    }

    tpl<RenderGraphCompileState, std::string> RenderGraph::Compile(RenderGraphCompileOptions options, RenderGraphDeviceContext ctx)
    {
        m_vulkanContext = ctx;

        m_Options = options;

        std::string msg;
        std::string prefix = "Render Graph compile time error:";
        
        if (m_HaveCompiled)
        {
            msg = "Render Graph can't be compiled twice";
            msg = prefix + msg;
            return std::make_tuple(RenderGraphCompileState::Error_CompileTwice, msg);
        }

        if (auto cres = ValidateCompileOptions(msg); cres != RenderGraphCompileState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(cres, msg);
        }
        if (auto cres = ValidateRenderPasses(msg); cres != RenderGraphCompileState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(cres, msg);
        }
        if (auto cres = CollectedResourceDependencies(msg); cres != RenderGraphCompileState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(cres, msg);
        }
        if (auto cres = BuildGraph(msg); cres != RenderGraphCompileState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(cres, msg);
        }

        {
            RenderGraphCompileState cres = RenderGraphCompileState::Success;
            if (m_Options.style == RenderGraphRenderPassStyle::OneByOne)
            {
                cres = ScheduleOneByOneGraph(msg);
            }
            else if (m_Options.style == RenderGraphRenderPassStyle::MergeGraphicsPasses)
            {
                cres = ScheduleMergedGraph(msg);
            }

            if (cres != RenderGraphCompileState::Success)
            {
                msg = prefix + msg;
                return std::make_tuple(cres, msg);
            }
        }

        if (auto cres = AssignPhysicalResources(msg); cres != RenderGraphCompileState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(cres, msg);
        }


        if (auto cres = ResolveDependenciesAndCreateRenderPasses(msg); cres != RenderGraphCompileState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(cres, msg);
        }
        
        ClearCompileCache();

        m_HaveCompiled = true;

        return tpl<RenderGraphCompileState, std::string>();
    }

    RenderGraphScope RenderGraph::Scope(const char* name)
    {
        return RenderGraphScope(name, this);
    }

    ResourceInfo RenderGraph::GetResourceInfo(ResourceHandle handle)
    {
        return m_LogicalResourceList[handle.idx].info;
    }

    void RenderGraph::OnResize(uint32_t width, uint32_t height)
    {
        m_Options.screenWidth = width;
        m_Options.screenHeight = height;

        ResizePhysicalResources();
    }

    tpl<RenderGraphRuntimeState, std::string> RenderGraph::Execute()
    {

        std::string prefix = "Render Graph Runtime Error:";
        std::string msg;

        if (auto rv = ValidateResourceBinding(msg);rv != RenderGraphRuntimeState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(rv, msg);
        }
        


    }

    RenderGraphDataFrame RenderGraph::GetExternalDataFrame()
    {
        vkrg_assert(m_HaveCompiled);
        return RenderGraphDataFrame(this, RenderGraphDataFrame::External);
    }

    RenderGraphRuntimeState RenderGraph::ValidateResourceBinding(std::string& msg)
    {
        
        for (uint32_t externalBindingIdx = 0; externalBindingIdx < m_ExternalResources.size(); externalBindingIdx++)
        {
            auto& binding = m_ExternalResourceBindings[externalBindingIdx];
            auto& res = m_ExternalResources[externalBindingIdx];
            
            for (uint32_t frameIdx = 0;frameIdx < m_Options.flightFrameCount; frameIdx++)
            {
                if (m_LogicalResourceList[res.handle.idx].info.IsBuffer())
                {
                    if (binding.buffers[frameIdx] == nullptr)
                    {
                        msg = "validation error : external resource " + res.name + "'s " + std::to_string(frameIdx) + " th frame's binding is missing";
                        return RenderGraphRuntimeState::Error_MissingExternalResourceAttachment;
                    }
                    else if (binding.buffers[frameIdx] == nullptr)
                    {
                        msg = "validation error : external resource " + res.name + "'s " + std::to_string(frameIdx) + "th frame's binding is missing";
                        return RenderGraphRuntimeState::Error_MissingExternalResourceAttachment;
                    }
                }
            }
        }

        return RenderGraphRuntimeState::Success;
    }

    RenderGraphCompileState RenderGraph::ValidateCompileOptions(std::string& msg)
    {
        if (m_Options.flightFrameCount < 1 || m_Options.flightFrameCount > 4)
        {
            msg = "invalid flight frame count, should be less than 4 and greater than 1";
            return RenderGraphCompileState::Error_InvalidCompileOption;
        }

        return RenderGraphCompileState::Success;
    }

    RenderGraphCompileState RenderGraph::ValidateRenderPasses(std::string& out_msg)
    {
        std::string msg = "";


        for (auto renderPassHandle : m_RenderPassList)
        {
            auto renderPass = renderPassHandle.pass;
            if (!renderPass->ValidationCheck(msg))
            {
                msg = std::string("Validation Error in Render Pass ") + renderPass->GetName() + " :" + msg;
                return RenderGraphCompileState::Error_RenderPassValidation;
            }

            auto& attachments = renderPass->GetAttachments();
            auto& resources = renderPass->GetAttachedResourceHandles();
            // tranverse every attachment
            for (uint32_t attachmentIdx = 0; attachmentIdx != attachments.size(); attachmentIdx++)
            {
                auto resource = resources[attachmentIdx];
                auto attachment = attachments[attachmentIdx];

                // add usage flag to corresponding resources automatically
                if (resource.external) continue;
                
                if (m_Options.addAutomaticTransferUsageFlag)
                {
                    if (m_LogicalResourceList[resource.idx].info.IsImage())
                    {
                        m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                        m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                    }
                    if (m_LogicalResourceList[resource.idx].info.IsBuffer())
                    {
                        m_LogicalResourceList[resource.idx].info.usages |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
                        m_LogicalResourceList[resource.idx].info.usages |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                    }
                }

                if (attachment.type == RenderPassAttachment::ImageColorOutput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
                }
                if (attachment.type == RenderPassAttachment::ImageDepthOutput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }
                if (attachment.type == RenderPassAttachment::ImageColorInput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
                }
                if (attachment.type == RenderPassAttachment::BufferStorageInput || attachment.type == RenderPassAttachment::ImageStorageOutput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_STORAGE_BIT;
                }
                if (attachment.type == RenderPassAttachment::BufferStorageInput || attachment.type == RenderPassAttachment::BufferStorageOutput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                }

                if (attachment.WriteToResource() && renderPass->RequireClearColor(attachment))
                {
                    msg = std::string("Validation Error in Render Pass ") + renderPass->GetName() + " :" + " nonoutput attachment can't be cleared";
                    return RenderGraphCompileState::Error_RenderPassValidation;
                }
            }
        }

        // check every resource if its format compatible with usage
        for (auto resource : m_LogicalResourceList)
        {
            bool res = false;
            if (resource.info.IsImage())
            {
                res = CheckImageUsageCompability(resource.info.format, resource.info.usages);
            }
            else if (resource.info.IsBuffer())
            {
                res = CheckBufferUsageCompability(resource.info.format, resource.info.usages);
            }

            if (!res)
            {
                msg = " resource '" + resource.name + "' has incompatible usage with this format";
                return RenderGraphCompileState::Error_RenderPassValidation;
            }
        }

        return RenderGraphCompileState::Success;
    }

    RenderGraphCompileState RenderGraph::CollectedResourceDependencies(std::string& msg)
    {
        m_LogicalResourceIODenpendencies.resize(m_LogicalResourceList.size());

        for (auto renderPassHandle : m_RenderPassList)
        {
            uint32_t renderPassIdx = renderPassHandle.idx;
            auto renderPass = renderPassHandle.pass;
            auto handles = renderPass->GetAttachedResourceHandles();
            auto attachments = renderPass->GetAttachments();

            for (uint32_t i = 0;i < handles.size(); i++)
            {
                uint32_t resourceIdx = handles[i].idx;
                if (attachments[i].WriteToResource())
                {
                    // Currently we don't support write after write to a resource
                    // Rather than write to the same resource, add a new resource and write to it is recommended
                    // Render Graph will merge redundant resource allocation automaticaly
                    if (!m_LogicalResourceIODenpendencies[resourceIdx].resourceWriteList.empty())
                    {
                        msg = " resource write after write occured during collecting dependencies for "  + m_LogicalResourceList[resourceIdx].name + 
                                " pass name : " + m_RenderPassList[renderPassIdx].pass->GetName()
                                + "\nRather than write to the same resource, add a new resource and write to it is recommended"
                                 " Render Graph will merge redundant resource allocation automaticaly";
                        return RenderGraphCompileState::Error_WriteAfterWrite;
                    }

                    m_LogicalResourceIODenpendencies[resourceIdx].resourceWriteList.push_back(renderPassIdx);
                }

                if (attachments[i].ReadFromResource())
                {
                    m_LogicalResourceIODenpendencies[resourceIdx].resourceReadList.push_back(renderPassIdx);
                }
            }
        }

        return RenderGraphCompileState::Success;
    }

    RenderGraphCompileState RenderGraph::BuildGraph(std::string& msg)
    {
        for (auto renderPassHandle : m_RenderPassList)
        {
            DAGNode node = m_Graph.AddNode(renderPassHandle);
            m_RenderPassNodeList.push_back(node);
        }

        for (auto denpendency : m_LogicalResourceIODenpendencies)
        {
            for (auto writeDenpendencyIdx : denpendency.resourceWriteList)
            {
                DAGNode writeDenpendencyNode = m_RenderPassNodeList[writeDenpendencyIdx];
                for (auto readDenpendencyIdx : denpendency.resourceReadList)
                {
                    DAGNode readDenpendencyNode = m_RenderPassNodeList[readDenpendencyIdx];
                    m_Graph.AddEdge(writeDenpendencyNode, readDenpendencyNode);
                }
            }
        }


        return RenderGraphCompileState::Success;
    }

    RenderGraphCompileState RenderGraph::ScheduleMergedGraph(std::string& msg)
    {
        if (!m_Graph.Sort())
        {
            msg = "graph contains cycle!";
            return RenderGraphCompileState::Error_CycleInGraph;
        }

        for (DAGNode currentNode = m_Graph.Begin(); currentNode != m_Graph.End(); currentNode++)
        {
            DAGMergedNode currentMergedNode;

            // found all merged depending render pass nodes
            // avoid redundant involving merged node searching
            std::vector<DAGMergedNode> dependingMergedNodes;
            std::vector<DAGNode>       inputNodes;
            for (DAGAdjNode currentInputNode = m_Graph.IterateAdjucentIn(currentNode);
                !currentInputNode.IsEnd(); currentInputNode++)
            {
                auto incomingMergedNode = FindInvolvedMergedPass(currentInputNode.CaseToNode());
                vkrg_assert(incomingMergedNode.has_value());

                auto [node, _] = incomingMergedNode.value();

                dependingMergedNodes.push_back(node);
                inputNodes.push_back(currentInputNode.CaseToNode());
            }

            auto checkPassClearValueRequirement = 
                [](ptr<RenderPass> rp)
            {
                for (auto attachment : rp->GetAttachments())
                {
                    if (rp->RequireClearColor(attachment))
                    {
                        return true;
                    }
                }
                return false;
            };

            // if this node is compute node, it can't be merged to any other nodes
            // create a single merged node for it
            if (currentNode->pass->GetType() != RenderPassType::Graphics)
            {
                currentMergedNode = CreateNewMergedNode(currentNode, false);
            }
            else
            {
                uint32_t currentMergedNodeScore = 0;

                // scan every dependencies, find the best merged render pass node to merge
                for (uint32_t i = 0; i < dependingMergedNodes.size(); i++)
                {
                    auto incomingMergedNode = dependingMergedNodes[i];
                    auto currentInputNode = inputNodes[i];

                    MergedRenderPass& mergedRenderPass = *incomingMergedNode;
                    // if this node can't be merged, skip this node
                    if (!mergedRenderPass.canBeMerged) continue;

                    // if adding this node to incoming merged node will create cycle in merged pass dependency graph
                    // skip this node
                    bool skipToAviodCycle = false;

                    for (uint32_t i = 0; i < dependingMergedNodes.size(); i++)
                    {
                        auto otherInputNode = inputNodes[i];
                        auto otherInputMergedNode = dependingMergedNodes[i];

                        if (otherInputNode != currentInputNode)
                        {
                            // if we merge to this incoming node, we will create a edge from incoming node to other node
                            // we will create a cycle when adding this edge if other node can reach incoming node
                            if (m_MergedRenderPassGraph.CanReach(otherInputMergedNode, incomingMergedNode))
                            {
                                skipToAviodCycle = true;
                                break;
                            }
                        }
                    }

                    if (skipToAviodCycle) continue;

                    
                    // find the node with highest score
                    uint32_t newMergedNodeScore = ScoreMergedNode(incomingMergedNode);
                    if (newMergedNodeScore > currentMergedNodeScore)
                    {
                        currentMergedNodeScore = newMergedNodeScore;
                        currentMergedNode = incomingMergedNode;
                    }
                }

                // if a node can be merged to is found
                // merge this render pass node to it
                if (!currentMergedNode.Invalid())
                {
                    currentMergedNode->renderPasses.push_back(currentNode);
                }
                // otherwise create a new node for this node
                else
                {
                    currentMergedNode = CreateNewMergedNode(currentNode, true);
                }
            }


            // add depending edges to m_MergedRenderPassGraph
            for (uint32_t i = 0; i < dependingMergedNodes.size(); i++)
            {
                auto incomingMergedNode = dependingMergedNodes[i];
                m_MergedRenderPassGraph.AddEdge(currentMergedNode, incomingMergedNode);
            }
        }

        // some thing goes wrong in our algorithm, otherwise sorting must be valid.
        vkrg_assert(m_MergedRenderPassGraph.Sort());

        return RenderGraphCompileState::Success;
    }

    RenderGraphCompileState RenderGraph::ScheduleOneByOneGraph(std::string& msg)
    {
        if (!m_Graph.Sort())
        {
            msg = "graph contains cycle!";
            return RenderGraphCompileState::Error_CycleInGraph;
        }

        for (DAGNode currentNode = m_Graph.Begin(); currentNode != m_Graph.End(); currentNode++)
        {
            DAGMergedNode currentMergedNode = CreateNewMergedNode(currentNode, false);

            DAGAdjNode currentInputNode = m_Graph.IterateAdjucentIn(currentNode);
            for (; !currentInputNode.IsEnd(); currentInputNode++)
            {
                auto inputMergedNode = FindInvolvedMergedPass(currentInputNode.CaseToNode());
                vkrg_assert(inputMergedNode.has_value());

                auto [node, _] = inputMergedNode.value();

                m_MergedRenderPassGraph.AddEdge(node, currentMergedNode);
            }
        }

        // some thing goes wrong in our algorithm, otherwise sorting must be valid.
        vkrg_assert(m_MergedRenderPassGraph.Sort());

        return RenderGraphCompileState::Success;
    }

    RenderGraphCompileState RenderGraph::AssignPhysicalResources(std::string& msg)
    {
        m_LogicalResourceAssignmentTable.resize(m_LogicalResourceList.size());
        std::unordered_set<uint32_t> visitedRenderPasses;

        // we prefer physical resource who might finished writing earliest
        auto physicalResourceScorer = [&](PhysicalResource& info)
        {
            uint32_t latestWriteEventIdx = 0;
            for (auto logicalResourceIdx : info.logicalResources)
            {
                uint32_t writeEventIdx = m_RenderPassNodeList[m_LogicalResourceIODenpendencies[logicalResourceIdx].resourceWriteList[0]].DistanceFromBegin();
                if (writeEventIdx > latestWriteEventIdx)
                {
                    latestWriteEventIdx = writeEventIdx;
                }
            }

            // physical resource finished writing early will get greater score
            return m_RenderPassList.size() - latestWriteEventIdx;
        };

        // tranverse the merged render graph
        // a resource can be reused if:
        // 1. all passes is depending on it are finished
        // 2. the resource matches currently required resource
        // 3. neither this resource nor currently required resource is an external resource
        // 4. currently required resource doesn't need clear

        // tranverse the merged render graph follow the topological order
        for (auto mergedRenderPassIter = m_MergedRenderPassGraph.Begin(); mergedRenderPassIter != m_MergedRenderPassGraph.End(); mergedRenderPassIter++)
        {
            // visit every render pass in this merged pass
            for (auto renderPass : mergedRenderPassIter->renderPasses)
            {
                visitedRenderPasses.insert(renderPass->idx);

                auto& attachments = renderPass->pass->GetAttachments();
                auto& resources = renderPass->pass->GetAttachedResourceHandles();
                // tranverse every output attachment
                for (uint32_t attachmentIdx = 0; attachmentIdx != attachments.size(); attachmentIdx++)
                {
                    ResourceHandle resource = resources[attachmentIdx];
                    RenderPassAttachment attachment = attachments[attachmentIdx];

                    // skip the external resources
                    if (resource.external) continue;

                    // skip already assigned resources
                    if (!m_LogicalResourceAssignmentTable[resource.idx].Invalid()) continue;

                    // skip input attachments
                    // we only allocate resources when render pass need to output
                    if (attachment.ReadFromResource()) continue;

                    // for image resources
                    if (m_LogicalResourceList[resource.idx].info.IsImage())
                    {
                        uint32_t physicalResourceIdx = 0;

                        // search for a allocated physical resource to assign
                        // if not, allocate a new physical resource
                        
                        uint32_t physicalResourceHightScore = 0;
                        bool     suitablePhysicalResourceFounded = false;

                        // 4. currently required resource doesn't need clear
                        // skip finding reusable resources for resources need clear
                        // this will promise every resource will be cleared only once per-frame
                        if (renderPass->pass->RequireClearColor(attachment))
                        {
                            // search existing allocated resources 
                            for (uint32_t currentPhysicalResourceIdx = 0; currentPhysicalResourceIdx != m_PhysicalResources.size(); currentPhysicalResourceIdx++)
                            {
                                PhysicalResource physicalResource = m_PhysicalResources[currentPhysicalResourceIdx];
                                // 2. the resource matches currently required resource
                                if (!ResourceCompability(physicalResource.info, m_LogicalResourceList[resource.idx].info))
                                {
                                    continue;
                                }

                                // 1. all passes is depending on it are finished
                                bool canBeAssigned = true;
                                for (auto assignedLogicalResourceIdx : physicalResource.logicalResources)
                                {
                                    for (auto dependingRenderPassIdx : m_LogicalResourceIODenpendencies[assignedLogicalResourceIdx].resourceReadList)
                                    {
                                        if (!visitedRenderPasses.count(dependingRenderPassIdx))
                                        {
                                            canBeAssigned = false;
                                            break;
                                        }
                                    }

                                    if (!canBeAssigned) break;
                                    // we don't have to check resource's write list, because the logical resource could only be assigned when its writer has been visited
                                }

                                // find the physical resource with highest score
                                if (canBeAssigned)
                                {
                                    uint32_t score = physicalResourceScorer(physicalResource);
                                    if (score > physicalResourceHightScore)
                                    {
                                        physicalResourceHightScore = score;
                                        physicalResourceIdx = currentPhysicalResourceIdx;
                                        suitablePhysicalResourceFounded = true;
                                    }
                                }
                            }
                        }
                        
                        // allocate a new resource
                        if (!suitablePhysicalResourceFounded)
                        {
                            PhysicalResource phyResource;
                            phyResource.info = m_LogicalResourceList[resource.idx].info;
                            phyResource.logicalResources.push_back(resource.idx);

                            physicalResourceIdx = m_PhysicalResources.size();
                            m_PhysicalResources.push_back(phyResource);
                        }

                        // merge newly assigned node's usage to physical node's usage
                        m_PhysicalResources[physicalResourceIdx].info.usages |= m_LogicalResourceList[resource.idx].info.usages;
                        // add logical node's index to physical node
                        m_PhysicalResources[physicalResourceIdx].logicalResources.push_back(resource.idx);

                        ResourceAssignment assignment;
                        assignment.external = resource.external;
                        assignment.idx = physicalResourceIdx;

                        m_LogicalResourceAssignmentTable[resource.idx] = assignment;
                    }
                    // for every buffer resources, we just allocate a new buffer and never reuse it
                    // TODO: better allocation strategy
                    else if(m_LogicalResourceList[resource.idx].info.IsBuffer())
                    {
                        PhysicalResource bufResource;
                        bufResource.info = m_LogicalResourceList[resource.idx].info;
                        bufResource.logicalResources.push_back(resource.idx);
                        uint32_t bufferResourceIdx = m_PhysicalResources.size();

                        ResourceAssignment assignment;
                        assignment.idx = bufferResourceIdx;
                        assignment.external = resource.external;
                        
                        m_PhysicalResources.push_back(bufResource);
                        m_LogicalResourceAssignmentTable[resource.idx] = assignment;
                    }
                    else
                    {
                        // this branch should not be reached!
                        vkrg_assert(false);
                    }

                    
                }

                // tranverse every external resources
                for (auto& logicalResource : m_LogicalResourceList)
                {
                    if (logicalResource.handle.external)
                    {
                        ExternalResource resource;
                        resource.handle = logicalResource.handle;
                        resource.name = logicalResource.name;

                        m_ExternalResources.push_back(resource);

                        ResourceAssignment assignment;
                        assignment.external = logicalResource.handle.external;
                        assignment.idx = m_ExternalResources.size() - 1;

                        m_LogicalResourceAssignmentTable[logicalResource.handle.idx] = assignment;
                    }
                }
            }
        }


        return RenderGraphCompileState::Success;
    }

    RenderGraphCompileState RenderGraph::ResolveDependenciesAndCreateRenderPasses(std::string& msg)
    {
        std::vector<VkImageLayout> physicalResourceLayouts(m_PhysicalResources.size(), VK_IMAGE_LAYOUT_UNDEFINED);
        std::vector<VkImageLayout> externalResourceLayouts(m_ExternalResources.size(), VK_IMAGE_LAYOUT_UNDEFINED);

        for (auto currentMergedPass : m_MergedRenderPasses)
        {
            RenderGraphPassInfo info;

            if (currentMergedPass->renderPasses[0]->pass->GetType() == RenderPassType::Compute)
            {
                vkrg_assert(currentMergedPass->renderPasses.size() == 1);

                auto computePass = currentMergedPass->renderPasses[0];

                info.type = RenderPassType::Compute;
                // TODO create barriers for compute shader

                // map logical resource to which barrier it is mapped
                std::vector<RenderGraphPassInfo::Compute::Handle> physicalResourceBarrierTable(m_PhysicalResources.size());
                // map external resource to which barrier it is mapped
                std::vector<RenderGraphPassInfo::Compute::Handle> externalResourceBarrierTable(m_ExternalResources.size());

                auto& attachments = computePass->pass->GetAttachments();
                auto& resources = computePass->pass->GetAttachedResourceHandles();
                for (uint32_t i = 0;i < attachments.size();i++)
                {
                    auto& attachment = attachments[i];
                    auto& resource = resources[i];

                    if (attachment.WriteToResource()) continue;

                    if (attachment.IsImage())
                    {
                        VkImageMemoryBarrier barrier{};

                        barrier.subresourceRange = attachment.range.imageRange;
                        auto assign = m_LogicalResourceAssignmentTable[resource.idx];
                        
                        if (assign.external)
                        {
                            // queue family index/ image field will be filled at runtime  
                            barrier.oldLayout = externalResourceLayouts[assign.idx];
                            barrier.newLayout = computePass->pass->GetAttachmentExpectedState(attachment);
                            externalResourceLayouts[assign.idx] = barrier.newLayout;
                        }
                        // assignment to physical resource
                        else
                        {
                            barrier.oldLayout = physicalResourceLayouts[assign.idx];
                            barrier.newLayout = computePass->pass->GetAttachmentExpectedState(attachment);
                            physicalResourceLayouts[assign.idx] = barrier.newLayout;
                        }
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                        barrier.pNext = NULL;

                        uint32_t barrierHandleIdx = info.compute.passImageBarriers.size();
                        info.compute.passImageBarriers.push_back(barrier);

                        uint32_t assignedResourceIdx = m_LogicalResourceAssignmentTable[resource.idx].idx;

                        RenderGraphPassInfo::Compute::Handle barrierHandle;
                        barrierHandle.idx = barrierHandleIdx;
                        barrierHandle.isBuffer = false;
                        if (assign.external)
                        {
                            externalResourceBarrierTable[assignedResourceIdx] = barrierHandle;
                        }
                        else
                        {
                            physicalResourceBarrierTable[assignedResourceIdx] = barrierHandle;
                        }
                    }
                    else if (attachment.IsBuffer())
                    {
                        VkBufferMemoryBarrier barrier{};
                        barrier.offset = attachment.range.bufferRange.offset;
                        barrier.size = attachment.range.bufferRange.size;
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        barrier.pNext = NULL;
                        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                        
                        uint32_t barrierHandleIdx = info.compute.passImageBarriers.size();
                        info.compute.passBufferBarriers.push_back(barrier);

                        uint32_t assignedResourceIdx = m_LogicalResourceAssignmentTable[resource.idx].idx;
                        auto assign = m_LogicalResourceAssignmentTable[resource.idx];

                        RenderGraphPassInfo::Compute::Handle barrierHandle;
                        barrierHandle.idx = barrierHandleIdx;
                        barrierHandle.isBuffer = true;

                        if (assign.external)
                        {
                            externalResourceBarrierTable[assignedResourceIdx] = barrierHandle;
                        }
                        else
                        {
                            physicalResourceBarrierTable[assignedResourceIdx] = barrierHandle;
                        }
                    }
                    else
                    {
                        // this branch should not be reached
                        vkrg_assert(false);
                    }

                }

                info.compute.externalResourceBarrierTable = externalResourceBarrierTable;
                info.compute.physicalResourceBarrierTable = physicalResourceBarrierTable;

            }
            else
            {
                std::vector<uint32_t> physicalResourceAttachmentTable(m_PhysicalResources.size(), invalidIdx);
                std::vector<uint32_t> logicalResourceAttachmentTable(m_LogicalResourceList.size(), invalidIdx);
                std::vector<uint32_t> externalResourceAttachmentTable(m_ExternalResources.size(), invalidIdx);

                struct FrameBufferAttachmentDescriptor
                {
                    uint32_t idx;
                    VkFormat format;

                    VkAttachmentLoadOp loadOp;
                    VkAttachmentStoreOp storeOp;
                    VkAttachmentLoadOp  stencilLoadOp;
                    VkAttachmentStoreOp stencilStoreOp;

                    VkImageLayout initLayout;
                    VkImageLayout finalLayout;

                };
                std::vector<FrameBufferAttachmentDescriptor>    frameBufferAttachmentDescs;
                std::vector<VkClearValue>                       frameBufferAttachmentClearColor;
                std::vector<RenderGraphPassInfo::FBAttachment>  frameBufferAttachments;

                GvkRenderPassCreateInfo vkRenderPassCreateInfo;

                // collecte all frame buffer resources for render pass
                info.type = RenderPassType::Graphics;
                for (auto renderPassNode : currentMergedPass->renderPasses)
                {
                    // tranverse all attachments find all resource attachments
                    auto& attachments = renderPassNode->pass->GetAttachments();
                    auto& resources = renderPassNode->pass->GetAttachedResourceHandles();
                    
                    // tranverse every attachment, collect all frame buffer attachments
                    for (uint32_t attachmentIdx = 0; attachmentIdx != attachments.size(); attachmentIdx++)
                    {
                        auto& resource = resources[attachmentIdx];
                        auto& attachment = attachments[attachmentIdx];

                        // if the logical resource has been assigned to a frame buffer attachment
                        // skip this resource 
                        if (logicalResourceAttachmentTable[resource.idx] != invalidIdx) continue;
                        
                        // skip the buffer resources
                        if (m_LogicalResourceList[resource.idx].info.IsBuffer()) continue;

                        // if the resource is a external resource
                        if (resource.external)
                        {
                            // the mapped external resource index of the logical resource index
                            auto& externalResourceIdx = m_LogicalResourceAssignmentTable[resource.idx].idx;

                            if (externalResourceAttachmentTable[externalResourceIdx] == invalidIdx)
                            {
                                uint32_t idx = frameBufferAttachmentDescs.size();

                                FrameBufferAttachmentDescriptor desc;
                                desc.idx = idx;
                                desc.format =  m_LogicalResourceList[resource.idx].info.format;
                                desc.initLayout = externalResourceLayouts[externalResourceIdx];

                                // make a initial guess according to attachment information
                                RenderPassAttachmentOperationState opState;
                                if (renderPassNode->pass->RequireClearColor(attachment))
                                {
                                    opState.load = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                    opState.store = VK_ATTACHMENT_STORE_OP_STORE;
                                }
                                else
                                {
                                    opState.load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                                    opState.store = VK_ATTACHMENT_STORE_OP_STORE;
                                }
                                opState.stencilLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                                opState.stencilStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;

                                renderPassNode->pass->GetAttachmentOperationState(attachment, opState);

                                desc.finalLayout = renderPassNode->pass->GetAttachmentExpectedState(attachment);

                                desc.loadOp = opState.load;
                                desc.storeOp = opState.store;
                                desc.stencilLoadOp = opState.stencilLoad;
                                desc.stencilStoreOp = opState.stencilStore;

                                externalResourceLayouts[externalResourceIdx] = renderPassNode->pass->GetAttachmentExpectedState(attachment);

                                VkClearValue clearValue{};
                                renderPassNode->pass->GetClearColor(attachment, clearValue);
                                frameBufferAttachmentClearColor.push_back(clearValue);

                                frameBufferAttachmentDescs.push_back(desc);
                                externalResourceAttachmentTable[externalResourceIdx] = idx;

                                frameBufferAttachments.push_back(m_LogicalResourceAssignmentTable[resource.idx]);
                            }
                            // althrough logical resource has not been assigned, physical resource has been assigned
                            // assign the physical resource's frame buffer index to logical resource
                            // this branch might not needed since external resoruce wouldn't be merged
                            else
                            {
                                // make a initial guess according to attachment information
                                RenderPassAttachmentOperationState opState;

                                renderPassNode->pass->GetAttachmentOperationState(attachment, opState);
                                FrameBufferAttachmentDescriptor& desc = frameBufferAttachmentDescs[externalResourceIdx]; 
                                desc.finalLayout = renderPassNode->pass->GetAttachmentExpectedState(attachment);

                                // overwrite the clear color value requirment
                                // this branch might not be reached
                                if (desc.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR && opState.load == VK_ATTACHMENT_LOAD_OP_CLEAR)
                                {
                                    desc.loadOp = opState.load;
                                    desc.storeOp = opState.store;
                                    desc.stencilLoadOp = opState.stencilLoad;
                                    desc.stencilStoreOp = opState.stencilStore;

                                    VkClearValue clearValue;
                                    renderPassNode->pass->GetClearColor(attachment, clearValue);
                                    frameBufferAttachmentClearColor[desc.idx] = clearValue;
                                }

                                externalResourceLayouts[externalResourceIdx] = renderPassNode->pass->GetAttachmentExpectedState(attachment);
                            }

                            logicalResourceAttachmentTable[resource.idx] = externalResourceAttachmentTable[externalResourceIdx];

                            // if the current render pass is the last pass write/read the external resource
                            // this is required for external resources like swap chain back buffer
                            if (FindLastAccessedNodeForResource(resource.idx) == currentMergedPass)
                            {
                                if (m_LogicalResourceList[resource.idx].finalLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                                {
                                    frameBufferAttachmentDescs[externalResourceAttachmentTable[externalResourceIdx]].finalLayout 
                                        = m_LogicalResourceList[resource.idx].finalLayout;
                                    externalResourceLayouts[resource.idx] = m_LogicalResourceList[resource.idx].finalLayout;
                                }
                            }
                        }
                        // if the resource is a physical resource
                        else
                        {
                            auto& physicalResourceIdx = m_LogicalResourceAssignmentTable[resource.idx].idx;

                            if (physicalResourceAttachmentTable[physicalResourceIdx] == invalidIdx)
                            {
                                uint32_t idx = frameBufferAttachmentDescs.size();

                                FrameBufferAttachmentDescriptor desc;
                                desc.idx = idx;
                                desc.format = m_LogicalResourceList[resource.idx].info.format;
                                desc.initLayout = physicalResourceLayouts[physicalResourceIdx];

                                // make a initial guess according to attachment information
                                RenderPassAttachmentOperationState opState;
                                if (renderPassNode->pass->RequireClearColor(attachment))
                                {
                                    opState.load = VK_ATTACHMENT_LOAD_OP_CLEAR;
                                    opState.store = VK_ATTACHMENT_STORE_OP_STORE;
                                }
                                else
                                {
                                    opState.load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                                    opState.store = VK_ATTACHMENT_STORE_OP_STORE;
                                }
                                opState.stencilLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                                opState.stencilStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;

                                renderPassNode->pass->GetAttachmentOperationState(attachment, opState);

                                desc.finalLayout = renderPassNode->pass->GetAttachmentExpectedState(attachment);

                                desc.loadOp = opState.load;
                                desc.storeOp = opState.store;
                                desc.stencilLoadOp = opState.stencilLoad;
                                desc.stencilStoreOp = opState.stencilStore;

                                VkClearValue clearValue{};
                                renderPassNode->pass->GetClearColor(attachment, clearValue);
                                frameBufferAttachmentClearColor.push_back(clearValue);

                                physicalResourceLayouts[physicalResourceIdx] = renderPassNode->pass->GetAttachmentExpectedState(attachment);

                                frameBufferAttachmentDescs.push_back(desc);
                                physicalResourceAttachmentTable[physicalResourceIdx] = idx;

                                frameBufferAttachments.push_back(m_LogicalResourceAssignmentTable[resource.idx]);
                            }
                            // althrough logical resource has not been assigned, physical resource has been assigned
                            // assign the physical resource's frame buffer index to logical resource
                            else
                            {
                                // make a initial guess according to attachment information
                                RenderPassAttachmentOperationState opState;
                                // one resource should not be cleared twice

                                renderPassNode->pass->GetAttachmentOperationState(attachment, opState);
                                FrameBufferAttachmentDescriptor& desc = frameBufferAttachmentDescs[physicalResourceIdx];
                                desc.finalLayout = renderPassNode->pass->GetAttachmentExpectedState(attachment);

                                // overwrite the clear color value requirment
                                // this branch might not be reached
                                if (desc.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR && opState.load == VK_ATTACHMENT_LOAD_OP_CLEAR)
                                {
                                    desc.loadOp = opState.load;
                                    desc.storeOp = opState.store;
                                    desc.stencilLoadOp = opState.stencilLoad;
                                    desc.stencilStoreOp = opState.stencilStore;

                                    VkClearValue clearValue;
                                    renderPassNode->pass->GetClearColor(attachment, clearValue);
                                    frameBufferAttachmentClearColor[desc.idx] = clearValue;
                                }

                                physicalResourceLayouts[physicalResourceIdx] = renderPassNode->pass->GetAttachmentExpectedState(attachment);
                            }

                            logicalResourceAttachmentTable[resource.idx] = physicalResourceAttachmentTable[physicalResourceIdx];
                        }
                    }
                    //
                }

                info.render.fbAttachmentIdx = frameBufferAttachments;

                // add frame buffer attachments
                for (uint32_t i = 0; i < frameBufferAttachmentDescs.size(); i++)
                {
                    auto& desc = frameBufferAttachmentDescs[i];
                    vkRenderPassCreateInfo.AddAttachment(
                        0, desc.format,
                        // TODO currently we only use 1 Sample count
                        VK_SAMPLE_COUNT_1_BIT,
                        desc.loadOp,
                        desc.storeOp,
                        desc.stencilLoadOp,
                        desc.stencilStoreOp,
                        desc.initLayout,
                        desc.finalLayout
                    );
                    info.render.fbClearValues = frameBufferAttachmentClearColor;
                }

                // create render pass
                for(auto renderPassNode : currentMergedPass->renderPasses)
                {
                    info.render.mergedSubpassIndices.push_back(renderPassNode->idx);
                    uint32_t currentSubpassIndex = vkRenderPassCreateInfo.AddSubpass();

                    // iterate all depending nodes
                    for (DAGAdjNode dependingPassNode = m_Graph.IterateAdjucentIn(renderPassNode); !dependingPassNode.IsEnd();
                        dependingPassNode++)
                    {
                        auto dependingMergedPassNodeTpl = FindInvolvedMergedPass(dependingPassNode.CaseToNode());
                        vkrg_assert(dependingMergedPassNodeTpl.has_value());

                        auto [dependingMergedPassNode, dependingPassNodeInternalIndex] = dependingMergedPassNodeTpl.value();

                        // depending subpass index in the depending pass node
                        uint32_t dependingPassIndex = 0;
                        // if the dependency comes from internal
                        if (dependingMergedPassNode == currentMergedPass)
                        {
                            dependingPassIndex = dependingPassNodeInternalIndex;
                        }
                        else
                        {
                            dependingPassIndex = VK_SUBPASS_EXTERNAL;
                        }

                        VkPipelineStageFlags dependingMergedPassNodeStage = 0;
                        VkAccessFlags dependingMemoryAccessFlag = 0;

                        RenderPassType dependingPassRpType = dependingPassNode.CaseToNode()->pass->GetType();
                        if (dependingPassRpType == RenderPassType::Compute)
                        {
                            dependingMergedPassNodeStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                            dependingMemoryAccessFlag = VK_ACCESS_MEMORY_WRITE_BIT;
                        }
                        else if (dependingPassRpType == RenderPassType::Graphics)
                        {
                            // TODO 
                            dependingMergedPassNodeStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                            dependingMemoryAccessFlag = VK_ACCESS_MEMORY_WRITE_BIT;
                        }

                        VkPipelineStageFlags currentMergedPassNodeStage = 0;
                        VkAccessFlags currentMemoryAccessFlag = 0;

                        RenderPassType currentPassRpType = renderPassNode->pass->GetType();
                        if (dependingPassRpType == RenderPassType::Compute)
                        {
                            currentMergedPassNodeStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                            currentMemoryAccessFlag = VK_ACCESS_MEMORY_WRITE_BIT;
                        }

                        // add subpass dependenices to this pass
                        vkRenderPassCreateInfo.AddSubpassDependency(
                            dependingPassIndex, currentSubpassIndex,
                            dependingMergedPassNodeStage, currentMergedPassNodeStage,
                            dependingMemoryAccessFlag, currentMemoryAccessFlag
                        );
                    }

                    // iterate all resource dependencies
                    auto& attachments = renderPassNode->pass->GetAttachments();
                    auto& resources = renderPassNode->pass->GetAttachedResourceHandles();

                    // tranverse every attachment, collect all frame buffer attachments
                    for (uint32_t attachmentIdx = 0; attachmentIdx != attachments.size(); attachmentIdx++)
                    {
                        auto& resource = resources[attachmentIdx];
                        auto& attachment = attachments[attachmentIdx];

                        if (m_LogicalResourceList[resource.idx].info.IsBuffer()) continue;

                        uint32_t fbAttachmentIdx = logicalResourceAttachmentTable[resource.idx];
                        vkrg_assert(fbAttachmentIdx != invalidIdx);
                        
                        RenderPassAttachmentOperationState opState{};
                        renderPassNode->pass->GetAttachmentOperationState(attachment, opState);
                        
                        vkrg_assert(attachment.type != RenderPassAttachment::ImageStorageInput && attachment.type != RenderPassAttachment::BufferStorageInput
                            && attachment.type != RenderPassAttachment::BufferStorageOutput && attachment.type != RenderPassAttachment::ImageStorageOutput);

                        if (attachment.type == RenderPassAttachment::ImageColorInput)
                        {
                            vkRenderPassCreateInfo.AddSubpassInputAttachment(currentSubpassIndex, fbAttachmentIdx, renderPassNode->pass->GetAttachmentExpectedState(attachment));
                        }
                        else if (attachment.type == RenderPassAttachment::ImageColorOutput)
                        {
                            vkRenderPassCreateInfo.AddSubpassColorAttachment(currentSubpassIndex, fbAttachmentIdx);
                        }
                        else if (attachment.type == RenderPassAttachment::ImageDepthOutput)
                        {
                            vkRenderPassCreateInfo.AddSubpassDepthStencilAttachment(currentSubpassIndex, fbAttachmentIdx);
                        }
                    }
                }

                if (auto rp = m_vulkanContext.ctx->CreateRenderPass(vkRenderPassCreateInfo); rp.has_value())
                {
                    info.render.renderPass = rp.value();
                }
                else
                {
                    msg = "fail to create vulkan render pass";
                    return RenderGraphCompileState::Error_FailToCreateRenderPass;
                }

                m_vulkanPassInfo.push_back(info);
            }
            
        }

        return RenderGraphCompileState::Success;
    }

    void RenderGraph::ResizePhysicalResources()
    {
        for (uint32_t physicalResourceIdx = 0; physicalResourceIdx != m_PhysicalResources.size(); physicalResourceIdx++)
        {
            auto& binding = m_PhysicalResourceBindings[physicalResourceIdx];
            const auto& info = m_PhysicalResources[physicalResourceIdx].info;

            binding.dirtyFlag = true;
            for (uint32_t i =0 ;i < m_Options.flightFrameCount;i++)
            {
                if (info.IsBuffer() && binding.buffers[i] == nullptr)
                {
                    binding.buffers[i] = nullptr;
                    auto res = m_vulkanContext.ctx->CreateBuffer(info.usages, info.ext.buffer.size, GVK_HOST_WRITE_NONE);
                    // this operation shouldn't fail
                    // 2 cases might cause failure
                    // 1. some thing goes wrong with our validation checker
                    // 2. out of memory
                    vkrg_assert(res.has_value());
                    binding.buffers[i] = res.value();
                }
                else if (info.IsImage())
                {
                    if (binding.buffers[i] != nullptr) continue;

                    GvkImageCreateInfo imageCI{};
                    
                    if (info.extType == ResourceExtensionType::Screen)
                    {
                        // scale the image by its screen size
                        imageCI.extent.width = (uint32_t)(info.ext.screen.x * (float)m_Options.screenWidth);
                        imageCI.extent.height = (uint32_t)(info.ext.screen.y * (float)m_Options.screenHeight);
                        imageCI.extent.depth = 1;
                    }
                    else if (info.extType == ResourceExtensionType::Fixed)
                    {
                        imageCI.extent.width = info.ext.fixed.x;
                        imageCI.extent.height = info.ext.fixed.y;
                        imageCI.extent.depth = info.ext.fixed.z;
                    }
                    
                    imageCI.arrayLayers = info.channelCount;
                    // TODO add flags
                    imageCI.flags = info.extraFlags;
                    imageCI.format = info.format;

                    if (info.expectedDimension != VK_IMAGE_TYPE_MAX_ENUM)
                    {
                        // decide by application
                        imageCI.imageType = info.expectedDimension;
                    }
                    else 
                    {
                        if (info.extType == ResourceExtensionType::Screen)
                        {
                            imageCI.imageType = VK_IMAGE_TYPE_2D;
                        }

                        else if (imageCI.extent.height <= 1 && imageCI.extent.depth <= 1)
                        {
                            imageCI.imageType = VK_IMAGE_TYPE_1D;
                        }
                        
                        else if (imageCI.extent.depth <= 1)
                        {
                            imageCI.imageType = VK_IMAGE_TYPE_2D;
                        }
                        else
                        {
                            imageCI.imageType = VK_IMAGE_TYPE_3D;
                        }

                    }
                    
                    imageCI.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
                    imageCI.mipLevels = info.mipCount;
                    imageCI.usage = info.usages;

                    auto res = m_vulkanContext.ctx->CreateImage(imageCI);
                    // this operation shouldn't fail
                    // 2 cases might cause failure
                    // 1. some thing goes wrong with our validation checker
                    // 2. out of memory
                    vkrg_assert(res.has_value());

                    binding.images[i] = res.value();
                }
                else
                {
                    // this branch should not be reached
                    vkrg_assert(false);
                }
            }
        }

    }

    void RenderGraph::UpdateDirtyFrameBuffers()
    {
    }

    void RenderGraph::ClearCompileCache()
    {
        m_Graph.Clear();
        m_MergedRenderPassGraph.Clear();
        m_RenderPassNodeList.clear();
        m_LogicalResourceIODenpendencies.clear();
        m_MergedRenderPasses.clear();
    }

    void RenderGraph::PostCompile()
    {
        m_PhysicalResourceBindings.resize(m_PhysicalResources.size());
        m_ExternalResourceBindings.resize(m_ExternalResources.size());

        ResizePhysicalResources();
        ClearCompileCache();
    }

    uint32_t RenderGraph::ScoreMergedNode(DAGMergedNode node)
    {
        // try to merge all node to one big node;
        // so the node merged with more nodes the better it is
        return node->renderPasses.size();
    }

    RenderGraph::DAGMergedNode RenderGraph::CreateNewMergedNode(DAGNode node, bool mergable)
    {
        MergedRenderPass pass{};
        pass.idx = m_MergedRenderPasses.size();
        pass.renderPasses.push_back(node);
        pass.canBeMerged = mergable;

        DAGMergedNode currentMergedNode = m_MergedRenderPassGraph.AddNode(pass);
        m_MergedRenderPasses.push_back(currentMergedNode);
        return currentMergedNode;
    }

    opt<tpl<RenderGraph::DAGMergedNode, uint32_t>> RenderGraph::FindInvolvedMergedPass(DAGNode node)
    {
        for (uint32_t i = 0;i < m_MergedRenderPasses.size();i++)
        {
            for (auto renderPassNode : (*m_MergedRenderPasses[i]).renderPasses)
            {
                if (renderPassNode == node)
                {
                    return std::make_tuple(m_MergedRenderPasses[i], i);
                }
            }
        }
        
        return std::nullopt;
    }

    RenderGraph::DAGMergedNode RenderGraph::FindLastAccessedNodeForResource(uint32_t logicalResourceIdx)
    {
        auto& dependency = m_LogicalResourceIODenpendencies[logicalResourceIdx];
        // a logical resource could only be written once
        
        DAGMergedNode node;
        if (auto tmp = FindInvolvedMergedPass(m_RenderPassNodeList[dependency.resourceWriteList[0]]); tmp.has_value())
        {
            auto [rv, _] = tmp.value();
            node = rv;
        }

        for (auto passIdx : dependency.resourceReadList)
        {
            DAGMergedNode candidate;
            {
                auto tmp = FindInvolvedMergedPass(m_RenderPassNodeList[passIdx]);
                vkrg_assert(tmp.has_value());
                auto [rv, _] = tmp.value();
                candidate = rv;
            }

            if (candidate > node)
            {
                node = candidate;
            }
        }

        return node;
    }

    bool RenderGraph::CheckImageUsageCompability(VkFormat format, VkImageUsageFlags usage)
    {
        // TODO 
        return true;
    }

    bool RenderGraph::CheckBufferUsageCompability(VkFormat format, VkBufferUsageFlags usages)
    {
        // TODO
        return false;
    }

    bool RenderGraph::ResourceCompability(ResourceInfo& lhs, ResourceInfo& rhs)
    {
        if (lhs.format != rhs.format || lhs.extType != rhs.extType)
        {
            return false;
        }

        bool rv = false;
        ResourceExtensionType extType = lhs.extType;
        if (extType == ResourceExtensionType::Screen)
        {
            rv |= vkrg_fequal(lhs.ext.screen.x, rhs.ext.screen.x);
            rv |= vkrg_fequal(lhs.ext.screen.y, rhs.ext.screen.y);

            rv |= lhs.mipCount == rhs.mipCount;
            rv |= lhs.channelCount == rhs.channelCount;
            rv |= CheckImageUsageCompability(lhs.format, lhs.usages | rhs.usages);
        }
        else if (extType == ResourceExtensionType::Fixed)
        {
            rv |= lhs.ext.fixed.x == rhs.ext.fixed.x;
            rv |= lhs.ext.fixed.y == rhs.ext.fixed.y;
            rv |= lhs.ext.fixed.z == rhs.ext.fixed.z;

            rv |= lhs.mipCount == rhs.mipCount;
            rv |= lhs.channelCount == rhs.channelCount;
            rv |= CheckImageUsageCompability(lhs.format, lhs.usages | rhs.usages);
        }
        else if (extType == ResourceExtensionType::Buffer)
        {
            rv |= lhs.ext.buffer.size == rhs.ext.buffer.size;
            rv |= CheckBufferUsageCompability(lhs.format, lhs.usages | rhs.usages);
        }

        return rv;
    }


    
    RenderGraphScope::RenderGraphScope(const char* name, RenderGraph* graph)
        :name(name), graph(graph)
    {}

    RenderGraphScope::RenderGraphScope(const RenderGraphScope& other)
        :name(name), graph(graph)
    {}

    opt<ResourceHandle> RenderGraphScope::FindGraphResource(const char* name)
    {
        return graph->FindGraphResource(GetScopeName(name).c_str());
    }

    opt<ResourceHandle> RenderGraphScope::GetGraphResource(uint32_t idx)
    {
        return graph->GetGraphResource(idx);
    }

    opt<RenderPassHandle> RenderGraphScope::FindGraphRenderPass(const char* name)
    {
        return graph->FindGraphRenderPass(GetScopeName(name).c_str());
    }

    opt<RenderPassHandle> RenderGraphScope::GetGraphRenderPass(uint32_t idx)
    {
        return graph->GetGraphRenderPass(idx);
    }

    opt<ResourceHandle> RenderGraphScope::AddGraphResource(const char* name, ResourceInfo info, bool external, VkImageLayout layout)
    {
        return graph->AddGraphResource(GetScopeName(name).c_str(), info, external, layout);
    }

    opt<RenderPassHandle> RenderGraphScope::AddGraphRenderPass(const char* name, RenderPassType type)
    {
        return graph->AddGraphRenderPass(GetScopeName(name).c_str(), type);
    }

    RenderGraphScope RenderGraphScope::Scope(const char* name)
    {
        return graph->Scope(GetScopeName(name).c_str());
    }


    std::string RenderGraphScope::GetScopeName(const char* name)
    {
        return this->name + "." + name;
    }
    
    bool RenderGraphDataFrame::BindBuffer(const char* name, uint32_t frameIdx, ptr<gvk::Buffer> buffer)
    {
        vkrg_assert(m_Graph->m_HaveCompiled);
        if (frameIdx >= m_FlightFrameCount) return false;

        RenderGraph::ResourceAssignment assign;
        auto resOpt = m_Graph->FindGraphResource(name);
        
        if (resOpt.has_value() && m_Graph->m_LogicalResourceList[resOpt.value().idx].info.IsBuffer())
        {
            assign = m_Graph->m_LogicalResourceAssignmentTable[resOpt.value().idx];
        }
        else
        {
            return false;
        }

        if (assign.Invalid() || (assign.external && m_Target != External) || (!assign.external && m_Target != Physical)) return false;
        
        m_Graph->m_ExternalResourceBindings[assign.idx].buffers[frameIdx] = buffer;

        return true;
    }

    bool RenderGraphDataFrame::BindImage(const char* name, uint32_t frameIdx, ptr<gvk::Image> image)
    {
        return true;
    }

    RenderGraphDataFrame::RenderGraphDataFrame(RenderGraph* graph, Target target)
        :m_Graph(graph), m_Target(target)
    {
        m_FlightFrameCount = m_Graph->m_Options.flightFrameCount;
    }

}
