#include "graph.h"


namespace vkrg
{
    RenderGraph::RenderGraph()
    {
        m_FormatCompabilityCache.initialized.resize(ResourceFormatCompabilityCache::supportedFormatCount, false);
        m_FormatCompabilityCache.formatProperties.resize(ResourceFormatCompabilityCache::supportedFormatCount);
    }

    opt<ResourceHandle> RenderGraph::FindGraphResource(const char* name)
    {
        if (auto iter = m_LogicalResourceTable.find(name); iter != m_LogicalResourceTable.end())
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
        if (auto iter = m_RenderPassTable.find(name); iter != m_RenderPassTable.end())
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
        // vkrg_assert(!external || !(info.IsImage() && layout == VK_IMAGE_LAYOUT_UNDEFINED));
        bool keepContent = (info.extraFlags & (uint32_t)ResourceExtraFlag::KeepContentFromLastFrame) != 0;
        // 当resource想保留上一帧内容时必须设置最终状态
        vkrg_assert(!keepContent || layout != VK_IMAGE_LAYOUT_UNDEFINED);

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

    opt<RenderPassHandle> RenderGraph::AddGraphRenderPass(const char* name, RenderPassType type, RenderPassExtension ext)
    {
        if (FindGraphRenderPass(name).has_value()) return std::nullopt;

        ptr<RenderPass> pass = std::make_shared<RenderPass>(this, name, type, ext);

        RenderPassHandle handle;
        handle.pass = pass;
        handle.idx = m_RenderPassList.size();

        m_RenderPassList.push_back(handle);
        m_RenderPassTable[name] = handle.idx;

        return handle;
    }

    void RenderGraph::AddEdge(RenderPassHandle outPass, RenderPassHandle inPass)
    {
        m_ExtraPassEdges.push_back({ outPass.idx, inPass.idx });
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

        PostCompile();

        m_HaveCompiled = true;

        return tpl<RenderGraphCompileState, std::string>(RenderGraphCompileState::Success, "");
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

    tpl<RenderGraphRuntimeState, std::string> RenderGraph::Execute(uint32_t targetFrameIdx, VkCommandBuffer mainCmdBuffer)
    {
        vkrg_assert(m_HaveCompiled);

        std::string prefix = "Render Graph Runtime Error:";
        std::string msg;

        if (auto rv = ValidateResourceBinding(msg); rv != RenderGraphRuntimeState::Success)
        {
            msg = prefix + msg;
            return std::make_tuple(rv, msg);
        }

        UpdateDirtyViews();
        UpdateDirtyFrameBuffersAndBarriers();

        GenerateCommands(mainCmdBuffer, targetFrameIdx);

        // reset all dirty flags at end of every frame primise RenderPassRuntimeContext::CheckAttachmentDirtyFlag will get right result
        ResetResourceBindingDirtyFlag();

        return std::make_tuple(RenderGraphRuntimeState::Success, "");
    }

    tpl<gvk::ptr<gvk::RenderPass>, uint32_t> RenderGraph::GetCompiledRenderPassAndSubpass(RenderPassHandle handle)
    {
        vkrg_assert(m_HaveCompiled);
        vkrg_assert(handle.pass->GetType() == RenderPassType::Graphics);

        auto [mergedPass, subpassIdx] = FindInvolvedMergedPass(m_RenderPassNodeList[handle.idx]).value();

        uint32_t renderGraphPassIndex = GetRenderGraphPassInfoIndex(mergedPass);
        vkrg_assert(m_renderGraphPassInfo.size() != renderGraphPassIndex);

        gvk::ptr<gvk::RenderPass> vkrp = m_renderGraphPassInfo[renderGraphPassIndex].render.renderPass;

        return std::make_tuple(vkrp, subpassIdx);
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

            for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
            {
                if (m_LogicalResourceList[res.handle.idx].info.IsBuffer())
                {
                    if (binding.buffers[frameIdx] == nullptr)
                    {
                        msg = "validation error : external resource " + res.name + "'s " + std::to_string(frameIdx) + " th frame's binding is missing";
                        return RenderGraphRuntimeState::Error_MissingExternalResourceAttachment;
                    }
                }
                else if (m_LogicalResourceList[res.handle.idx].info.IsImage())
                {
                    if (binding.images[frameIdx] == nullptr)
                    {
                        msg = "validation error : external resource " + res.name + "'s " + std::to_string(frameIdx) + " th frame's binding is missing";
                        return RenderGraphRuntimeState::Error_MissingExternalResourceAttachment;
                    }
                }
            }
        }


        return RenderGraphRuntimeState::Success;
    }

    RenderGraphCompileState RenderGraph::ValidateCompileOptions(std::string& msg)
    {
        if (m_Options.flightFrameCount < 1 || m_Options.flightFrameCount > maxFrameOnFlightCount)
        {
            msg = "invalid flight frame count, should be less than " + std::to_string(maxFrameOnFlightCount) + " and greater than 1";
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
                out_msg = std::string("Validation Error in Render Pass ") + renderPass->GetName() + " :" + msg;
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
                if (attachment.type == RenderPassAttachment::ImageDepthOutput || attachment.type == RenderPassAttachment::ImageDepthInput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                }
                if (attachment.type == RenderPassAttachment::ImageColorInput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
                }
                if (attachment.type == RenderPassAttachment::ImageRTSampledInput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_SAMPLED_BIT;
                }
                if (attachment.type == RenderPassAttachment::ImageStorageInput || attachment.type == RenderPassAttachment::ImageStorageOutput
                    || attachment.type == RenderPassAttachment::ImageRTInput || attachment.type == RenderPassAttachment::ImageRTOutput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_IMAGE_USAGE_STORAGE_BIT;
                }
                if (attachment.type == RenderPassAttachment::BufferStorageInput || attachment.type == RenderPassAttachment::BufferStorageOutput
                    || attachment.type == RenderPassAttachment::BufferRTInput || attachment.type == RenderPassAttachment::BufferRTOutput)
                {
                    m_LogicalResourceList[resource.idx].info.usages |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                }


                if (attachment.ReadFromResource() && renderPass->RequireClearColor(attachment))
                {
                    out_msg = std::string("Validation Error in Render Pass ") + renderPass->GetName() + " :" + " nonoutput attachment can't be cleared";
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
                out_msg = " resource '" + resource.name + "' has incompatible usage with this format";
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

            for (uint32_t i = 0; i < handles.size(); i++)
            {
                uint32_t resourceIdx = handles[i].idx;
                if (attachments[i].WriteToResource())
                {
                    // Currently we don't support write after write to a resource
                    // Rather than write to the same resource, add a new resource and write to it is recommended
                    // Render Graph will merge redundant resource allocation automaticaly
                    if (!m_LogicalResourceIODenpendencies[resourceIdx].resourceWriteList.empty())
                    {
                        msg = " resource write after write occured during collecting dependencies for " + m_LogicalResourceList[resourceIdx].name +
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

        for (auto extraEdge : m_ExtraPassEdges)
        {
            DAGNode outPassNode = m_RenderPassNodeList[extraEdge.outPassIdx];
            DAGNode inPassNode = m_RenderPassNodeList[extraEdge.inPassIdx];

            m_Graph.AddEdge(outPassNode, inPassNode);
        }


        return RenderGraphCompileState::Success;
    }

    template<typename T>
    void PushBackNotRepeatedElement(std::vector<T>& vec, const T& elem)
    {
        if (std::find(vec.begin(), vec.end(), elem) == vec.end())
        {
            vec.push_back(elem);
        }
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

                PushBackNotRepeatedElement(dependingMergedNodes, node);
                PushBackNotRepeatedElement(inputNodes, currentInputNode.CaseToNode());
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
                currentMergedNode->expectedExtension = currentNode->pass->GetRenderPassExtension();
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

                    // if this node doesn't match the expected extension， skip this node
                    if (!(mergedRenderPass.expectedExtension == currentInputNode->pass->GetRenderPassExtension())) continue;

                    // if adding this node to incoming merged node will create cycle in merged pass dependency graph
                    // skip this node
                    bool skipToAviodCycle = false;

                    for (uint32_t i = 0; i < dependingMergedNodes.size(); i++)
                    {
                        auto otherInputNode = inputNodes[i];
                        auto otherInputMergedNode = dependingMergedNodes[i];

                        // 基于当前选定的merged node，我们检查其是否能到达当前node依赖的其它merge node
                        // 若能到达则如果将当前node合并到选定的merged node中就会在依赖图中构造环
                        // 这种情况下，我们不能将该node合并到merged node中
                        if (otherInputNode != currentInputNode)
                        {
                            // if we merge to this incoming node, we will create a edge from incoming node to other node
                            // we will create a cycle when adding this edge if other node can reach incoming node
                            if (m_MergedRenderPassGraph.CanReach(incomingMergedNode, otherInputMergedNode))
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
                if (currentMergedNode != incomingMergedNode)
                {
                    m_MergedRenderPassGraph.AddEdge(incomingMergedNode, currentMergedNode);
                }
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

                                    // the resource can't be reused if the resource is written in the same pass
                                    canBeAssigned = m_LogicalResourceIODenpendencies[assignedLogicalResourceIdx].resourceWriteList[0] != renderPass->idx;

                                    // the resource can't be reused if the resource's final layout is decided
                                    canBeAssigned &= m_LogicalResourceList[assignedLogicalResourceIdx].finalLayout == VK_IMAGE_LAYOUT_UNDEFINED;

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
                            phyResource.finalLayout = m_LogicalResourceList[resource.idx].finalLayout;

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
                    else if (m_LogicalResourceList[resource.idx].info.IsBuffer())
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
                resource.finalLayout = logicalResource.finalLayout;

                m_ExternalResources.push_back(resource);

                ResourceAssignment assignment;
                assignment.external = logicalResource.handle.external;
                assignment.idx = m_ExternalResources.size() - 1;

                m_LogicalResourceAssignmentTable[logicalResource.handle.idx] = assignment;
            }
        }

        return RenderGraphCompileState::Success;
    }


    // a helper structure record states during transitions
    struct ImageLayoutStatus
    {
        ImageLayoutStatus(ResourceInfo info) : info(info)
        {
            subresourceLayouts.resize(info.channelCount * info.mipCount * GetAspectCount(info.format), VK_IMAGE_LAYOUT_UNDEFINED);
        }

        ImageLayoutStatus(ResourceInfo info, VkImageLayout initLayout) : info(info)
        {
            subresourceLayouts.resize(info.channelCount * info.mipCount * GetAspectCount(info.format), initLayout);
        }


        VkImageLayout Query(ImageSlice subresource)
        {
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            bool flag = false;

            // VkImage of format VK_FORMAT_D24_UNORM_S8_UINT that must have the depth and stencil aspects set, 
            // but its aspectMask is 0x2. The Vulkan spec states: If image has a depth/stencil format with both 
            // depth and stencil and the separateDepthStencilLayouts feature is not enabled, then the aspectMask 
            // member of subresourceRange must include both VK_IMAGE_ASPECT_DEPTH_BIT and VK_IMAGE_ASPECT_STENCIL_BIT
            if (subresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT || subresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
            {
                subresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            auto op = [&](uint32_t idx)
            {
                VkImageLayout subresourceLayout = subresourceLayouts[idx];
                if (!flag)
                {
                    layout = subresourceLayout;
                    flag = true;
                }
                else
                {
                    if (layout != subresourceLayout) layout = VK_IMAGE_LAYOUT_UNDEFINED;
                }
            };
            SubresourceRange(subresource, op);

            return layout;
        }


        void          Update(ImageSlice subresource, VkImageLayout layout)
        {
            auto op = [&](uint32_t idx)
            {
                subresourceLayouts[idx] = layout;
            };

            // VkImage of format VK_FORMAT_D24_UNORM_S8_UINT that must have the depth and stencil aspects set, 
            // but its aspectMask is 0x2. The Vulkan spec states: If image has a depth/stencil format with both 
            // depth and stencil and the separateDepthStencilLayouts feature is not enabled, then the aspectMask 
            // member of subresourceRange must include both VK_IMAGE_ASPECT_DEPTH_BIT and VK_IMAGE_ASPECT_STENCIL_BIT
            if (subresource.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT || subresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
            {
                subresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            SubresourceRange(subresource, op);
        }

        static ImageSlice Merge(ImageSlice lhs, ImageSlice rhs)
        {
            ImageSlice slice;
            slice.aspectMask = lhs.aspectMask | rhs.aspectMask;
            slice.baseArrayLayer = vkrg_min(lhs.baseArrayLayer, rhs.baseArrayLayer);
            slice.baseMipLevel = vkrg_min(lhs.baseMipLevel, rhs.baseMipLevel);

            uint32_t maxMipLevel = vkrg_max(lhs.baseMipLevel + lhs.levelCount, rhs.baseMipLevel + rhs.levelCount);
            uint32_t maxArrayLayer = vkrg_max(lhs.baseArrayLayer + lhs.layerCount, rhs.baseArrayLayer + rhs.layerCount);

            slice.layerCount = maxArrayLayer - slice.baseArrayLayer;
            slice.baseMipLevel = maxMipLevel - slice.baseMipLevel;

            return slice;
        }

    private:
        void SubresourceRange(ImageSlice slice, std::function<void(uint32_t)> op)
        {
            auto aspectRange = [&](VkImageAspectFlagBits flag)
            {
                if (slice.aspectMask & flag)
                {
                    uint32_t offset0 = GetAspectIndex(flag) * info.channelCount * info.mipCount;
                    for (uint32_t arrIdx = 0; arrIdx < slice.layerCount; arrIdx++)
                    {
                        for (uint32_t mipIdx = 0; mipIdx < slice.levelCount; mipIdx++)
                        {
                            uint32_t idx = mipIdx + arrIdx * info.mipCount + offset0;
                            op(idx);
                        }
                    }
                }
            };
            aspectRange(VK_IMAGE_ASPECT_COLOR_BIT);
            aspectRange(VK_IMAGE_ASPECT_DEPTH_BIT);
            aspectRange(VK_IMAGE_ASPECT_STENCIL_BIT);
        }
        uint32_t GetAspectCount(VkFormat format)
        {
            VkImageAspectFlags flags = gvk::GetAllAspects(info.format);
            if (flags == VK_IMAGE_ASPECT_COLOR_BIT)
            {
                return 1;
            }
            if (flags == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_DEPTH_BIT))
            {
                return 2;
            }
        }

        uint32_t GetAspectIndex(VkImageAspectFlagBits flag)
        {
            if (flag == VK_IMAGE_ASPECT_COLOR_BIT)
            {
                return 0;
            }
            if (flag == VK_IMAGE_ASPECT_DEPTH_BIT)
            {
                return 0;
            }
            if (flag == VK_IMAGE_ASPECT_STENCIL_BIT)
            {
                return 1;
            }
            return 0;
        }
        ResourceInfo info;
        std::vector<VkImageLayout> subresourceLayouts;
    };

    // a helper structure dealing with frame buffer attachment
    struct ImageFBAttachmentStatus
    {
    public:
        static constexpr uint32_t invalidIdx = 0xffffffff;

        ImageFBAttachmentStatus(ResourceInfo info) : info(info)
        {
            if (info.IsBuffer())  return;
        }

        uint32_t Query(ImageSlice subresource)
        {
            uint32_t internalIdx = QueryInternal(subresource);
            if (internalIdx != invalidIdx)
            {
                return targetFBAttachmentIdx[internalIdx];
            }
            return invalidIdx;
        }


        void     Update(ImageSlice subresource, uint32_t idx)
        {
            uint32_t internalIdx = QueryInternal(subresource);
            if (internalIdx == invalidIdx)
            {
                slices.push_back(subresource);
                targetFBAttachmentIdx.push_back(idx);
            }
        }

        //http://geekfaner.com/shineengine/blog18_Vulkanv1.2_4.html
        //如果一个render pass使用多个attachment alias相同的device memory，则每个attachment都必须包含 VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT 。attachments alias相同内存有如下几种方式：

        //  多个attachment在创建framebuffer的时候，对应同一个image view
        //  多个attachment对应同一个image的同一个image subresource的不同image view
        //  多个attachment对应views of distinct image subresources bound to 覆盖的内存范围
        bool MayAlias()
        {
            return slices.size() > 1;
        }


    private:

        uint32_t QueryInternal(ImageSlice subresource)
        {
            for (uint32_t i = 0; i < slices.size(); i++)
            {
                if (slices[i].aspectMask == subresource.aspectMask &&
                    slices[i].baseArrayLayer == subresource.baseArrayLayer &&
                    slices[i].baseMipLevel == subresource.baseMipLevel &&
                    slices[i].layerCount == subresource.layerCount &&
                    slices[i].levelCount == subresource.levelCount)
                {
                    return i;
                }
            }
            return invalidIdx;
        }


        std::vector<ImageSlice> slices;
        std::vector<uint32_t>   targetFBAttachmentIdx;

        ResourceInfo info;
    };


    struct ImageBarrierHelper
    {
        ImageBarrierHelper(uint32_t frameCount)
        {
            m_frameCount = frameCount;
        }

        void AddImage(VkPipelineStageFlagBits srcStage, VkPipelineStageFlagBits dstStage, VkImageMemoryBarrier imgBarrier, RenderGraphBarrier::Handle handle)
        {
            RenderGraphBarrier& barrier = FindBarrier(srcStage, dstStage);
            for (uint32_t i = 0; i < m_frameCount; i++)
            {
                barrier.imageBarriers[i].push_back(imgBarrier);
            }
            barrier.imageBarrierHandles.push_back(handle);
        }
        void AddBuffer(VkPipelineStageFlagBits srcStage, VkPipelineStageFlagBits dstStage, VkBufferMemoryBarrier bufferBarrier, RenderGraphBarrier::Handle handle)
        {
            RenderGraphBarrier& barrier = FindBarrier(srcStage, dstStage);

            for (uint32_t i = 0; i < m_frameCount; i++)
            {
                barrier.bufferBarriers[i].push_back(bufferBarrier);
            }
            barrier.bufferBarrierHandles.push_back(handle);
        }

        std::vector<RenderGraphBarrier> barriers;
        uint32_t m_frameCount;

    private:
        RenderGraphBarrier& FindBarrier(VkPipelineStageFlagBits srcStage, VkPipelineStageFlagBits dstStage)
        {
            for (auto& barrier : barriers)
            {
                if (barrier.srcStage == srcStage && barrier.dstStage == dstStage)
                {
                    return barrier;
                }
            }

            RenderGraphBarrier barrier;
            barrier.srcStage = srcStage;
            barrier.dstStage = dstStage;

            barriers.push_back(barrier);

            return barriers[barriers.size() - 1];
        }
    };

    RenderGraphCompileState RenderGraph::ResolveDependenciesAndCreateRenderPasses(std::string& msg)
    {
        std::vector<ImageLayoutStatus> physicalResourceLayouts;
        std::vector<ImageLayoutStatus> externalResourceLayouts;

        ImageBarrierHelper globalBarrierHelper(m_Options.flightFrameCount);

        // initialize image layout lists
        for (auto physicalResource : m_PhysicalResources)
        {
            // 如果resource选择保留上一帧的内容，则第一个barrier的oldLayout为上一帧的finalLayout。否则，我们可以不用关心上一帧resource的状态是什么
            // 这种情况下，resource的oldLayout为 undefined

            bool keepLastFrameContent = (physicalResource.info.extraFlags & (uint32_t)ResourceExtraFlag::KeepContentFromLastFrame) != 0;
            if (keepLastFrameContent)
            {
                physicalResourceLayouts.push_back(ImageLayoutStatus(physicalResource.info, physicalResource.finalLayout));
            }
            else
            {
                physicalResourceLayouts.push_back(ImageLayoutStatus(physicalResource.info));
            }
        }
        for (auto externalResource : m_ExternalResources)
        {
            // the init layout in this frame is final layout from last frame

            bool keepLastFrameContent = (m_LogicalResourceList[externalResource.handle.idx].info.extraFlags & (uint32_t)ResourceExtraFlag::KeepContentFromLastFrame) != 0;
            if (keepLastFrameContent)
            {
                externalResourceLayouts.push_back(ImageLayoutStatus(m_LogicalResourceList[externalResource.handle.idx].info, externalResource.finalLayout));
            }
            else
            {
                externalResourceLayouts.push_back(ImageLayoutStatus(m_LogicalResourceList[externalResource.handle.idx].info));
            }
        }

        for (auto currentMergedPass = m_MergedRenderPassGraph.Begin(); currentMergedPass != m_MergedRenderPassGraph.End(); currentMergedPass++)
        {
            RenderGraphPassInfo info;

            ImageBarrierHelper barrierHelper(m_Options.flightFrameCount);

            if (currentMergedPass->renderPasses[0]->pass->IsGeneralPass())
            {
                vkrg_assert(currentMergedPass->renderPasses.size() == 1);

                auto currentPass = currentMergedPass->renderPasses[0];

                info.type = currentMergedPass->renderPasses[0]->pass->GetType();

                auto& attachments = currentPass->pass->GetAttachments();
                auto& resources = currentPass->pass->GetAttachedResourceHandles();

                for (uint32_t i = 0; i < attachments.size(); i++)
                {
                    auto& attachment = attachments[i];
                    auto& resource = resources[i];

                    /*
                    if (attachment.WriteToResource() && FindLastAccessedNodeForResource(resource.idx) != currentMergedPass
                        && FindFirstAccessedNodeForResource(resource.idx) != currentMergedPass)
                    {
                        continue;
                    }
                    */

                    if (attachment.IsImage())
                    {
                        VkImageMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.subresourceRange = attachment.range.imageRange;

                        // VkImage of format VK_FORMAT_D24_UNORM_S8_UINT that must have the depth and stencil aspects set, 
                        // but its aspectMask is 0x2. The Vulkan spec states: If image has a depth/stencil format with both 
                        // depth and stencil and the separateDepthStencilLayouts feature is not enabled, then the aspectMask 
                        // member of subresourceRange must include both VK_IMAGE_ASPECT_DEPTH_BIT and VK_IMAGE_ASPECT_STENCIL_BIT
                        if (attachment.range.imageRange.aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT || attachment.range.imageRange.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
                        {
                            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                        }
                        auto assign = m_LogicalResourceAssignmentTable[resource.idx];
                        VkImageLayout newStateLayout = currentPass->pass->GetAttachmentExpectedState(attachment);

                        if (assign.external)
                        {
                            // queue family index/ image field will be filled at runtime  
                            barrier.oldLayout = externalResourceLayouts[assign.idx].Query(attachment.range.imageRange);
                            barrier.newLayout = newStateLayout;
                            externalResourceLayouts[assign.idx].Update(attachment.range.imageRange, barrier.newLayout);
                        }
                        // assignment to physical resource
                        else
                        {
                            barrier.oldLayout = physicalResourceLayouts[assign.idx].Query(attachment.range.imageRange);
                            barrier.newLayout = currentPass->pass->GetAttachmentExpectedState(attachment);
                            physicalResourceLayouts[assign.idx].Update(attachment.range.imageRange, barrier.newLayout);
                        }
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
                        barrier.pNext = NULL;

                        auto& dependency = m_LogicalResourceIODenpendencies[resource.idx];
                        auto& writer = m_RenderPassList[dependency.resourceWriteList[0]];

                        VkPipelineStageFlagBits srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;//, dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                        VkPipelineStageFlagBits dstStage = info.type == RenderPassType::Compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;



                        if (writer.pass->GetType() == RenderPassType::Compute)
                        {
                            srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                        }
                        else if (writer.pass->GetType() == RenderPassType::Raytracing)
                        {
                            srcStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
                        }
                        else
                        {
                            if (gvk::GetAllAspects(m_LogicalResourceList[resource.idx].info.format) & VK_IMAGE_ASPECT_DEPTH_BIT)
                            {
                                srcStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
                            }
                            else
                            {
                                srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                            }
                        }

                        RenderGraphBarrier::Handle handle;
                        handle.idx = assign.idx;
                        handle.external = assign.external;

                        barrierHelper.AddImage(srcStage, dstStage, barrier, handle);
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

                        ResourceAssignment assign;
                        assign = m_LogicalResourceAssignmentTable[resource.idx];

                        RenderGraphBarrier::Handle handle;
                        handle.idx = assign.idx;
                        handle.external = assign.external;

                        // TODO better source stage flag
                        // render pass will not write to buffer so we can assume the render pass ahead is compute render pass
                        VkPipelineStageFlagBits srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;//, dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                        VkPipelineStageFlagBits dstStage = info.type == RenderPassType::Compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
                        barrierHelper.AddBuffer(srcStage, dstStage, barrier, handle);
                    }
                    else
                    {
                        // this branch should not be reached
                        vkrg_assert(false);
                    }

                    if (FindLastAccessedNodeForResource(resource.idx) == currentMergedPass && m_LogicalResourceList[resource.idx].finalLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                    {
                        VkPipelineStageFlagBits srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;//, dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                        VkPipelineStageFlagBits dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

                        VkImageMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.subresourceRange = attachment.range.imageRange;

                        auto assign = m_LogicalResourceAssignmentTable[resource.idx];
                        if (assign.external)
                        {
                            // queue family index/ image field will be filled at runtime  
                            barrier.oldLayout = externalResourceLayouts[assign.idx].Query(attachment.range.imageRange);
                            barrier.newLayout = m_LogicalResourceList[resource.idx].finalLayout;
                            externalResourceLayouts[assign.idx].Update(attachment.range.imageRange, barrier.newLayout);
                        }
                        // assignment to physical resource
                        else
                        {
                            barrier.oldLayout = physicalResourceLayouts[assign.idx].Query(attachment.range.imageRange);
                            barrier.newLayout = m_LogicalResourceList[resource.idx].finalLayout;
                            physicalResourceLayouts[assign.idx].Update(attachment.range.imageRange, barrier.newLayout);
                        }

                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.srcAccessMask = VK_ACCESS_NONE;
                        barrier.dstAccessMask = VK_ACCESS_NONE;
                        barrier.pNext = NULL;

                        RenderGraphBarrier::Handle handle;
                        handle.idx = assign.idx;
                        handle.external = assign.external;

                        globalBarrierHelper.AddImage(srcStage, dstStage, barrier, handle);
                    }
                }

                info.compute.targetRenderPass = currentMergedPass->renderPasses[0]->idx;
            }
            else
            {
                std::vector<ImageFBAttachmentStatus> physicalResourceAttachmentTable;
                std::vector<ImageFBAttachmentStatus> externalResourceAttachmentTable;

                for (uint32_t i = 0; i < m_PhysicalResources.size(); i++)
                {
                    physicalResourceAttachmentTable.push_back(ImageFBAttachmentStatus(m_PhysicalResources[i].info));
                }
                for (uint32_t i = 0; i < m_ExternalResources.size(); i++)
                {
                    externalResourceAttachmentTable.push_back(ImageFBAttachmentStatus(m_LogicalResourceList[m_ExternalResources[i].handle.idx].info));
                }

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

                        // skip the buffer resources
                        // TODO barrier for buffer resource
                        if (m_LogicalResourceList[resource.idx].info.IsBuffer()) continue;

                        // if the resource is a external resource
                        if (resource.external)
                        {
                            // the mapped external resource index of the logical resource index
                            auto& externalResourceIdx = m_LogicalResourceAssignmentTable[resource.idx].idx;
                            uint32_t externalResourceAttachmentIdx = externalResourceAttachmentTable[externalResourceIdx].Query(attachment.range.imageRange);

                            if (externalResourceAttachmentTable[externalResourceIdx].Query(attachment.range.imageRange) == ImageFBAttachmentStatus::invalidIdx ||
                                frameBufferAttachments[externalResourceAttachmentIdx].viewType != attachment.viewType)
                            {
                                uint32_t idx = frameBufferAttachmentDescs.size();

                                FrameBufferAttachmentDescriptor desc;
                                desc.idx = idx;
                                desc.format = m_LogicalResourceList[resource.idx].info.format;
                                desc.initLayout = externalResourceLayouts[externalResourceIdx].Query(attachment.range.imageRange);

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

                                externalResourceLayouts[externalResourceIdx].Update(attachment.range.imageRange, desc.finalLayout);

                                VkClearValue clearValue{};
                                renderPassNode->pass->GetClearColor(attachment, clearValue);
                                frameBufferAttachmentClearColor.push_back(clearValue);

                                // if the current render pass is the last pass write/read the external resource
                            // this is required for external resources like swap chain back buffer
                                if (FindLastAccessedNodeForResource(resource.idx) == currentMergedPass)
                                {
                                    if (m_LogicalResourceList[resource.idx].finalLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                                    {
                                        desc.finalLayout = m_LogicalResourceList[resource.idx].finalLayout;
                                        externalResourceLayouts[externalResourceIdx].Update(attachment.range.imageRange, m_LogicalResourceList[resource.idx].finalLayout);
                                    }
                                }

                                frameBufferAttachmentDescs.push_back(desc);
                                externalResourceAttachmentTable[externalResourceIdx].Update(attachment.range.imageRange, idx);

                                RenderGraphPassInfo::FBAttachment fbAttachment;
                                fbAttachment.assign = m_LogicalResourceAssignmentTable[resource.idx];
                                fbAttachment.subresource = attachment.range.imageRange;
                                fbAttachment.viewType = attachment.viewType;

                                frameBufferAttachments.push_back(fbAttachment);
                            }
                            // althrough logical resource has not been assigned, physical resource has been assigned
                            // assign the physical resource's frame buffer index to logical resource
                            // this branch might not needed since external resoruce wouldn't be merged
                            else
                            {
                                // TODO currently we assume this case doesn't exists
                                vkrg_assert(false);
                                /*
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

                                externalResourceLayouts[externalResourceIdx].Update(attachment.range.imageRange, desc.finalLayout);
                                */
                            }


                        }
                        // if the resource is a physical resource
                        else
                        {
                            auto& physicalResourceIdx = m_LogicalResourceAssignmentTable[resource.idx].idx;
                            uint32_t physicalResourceAttachmentIdx = physicalResourceAttachmentTable[physicalResourceIdx].Query(attachment.range.imageRange);

                            if (physicalResourceAttachmentIdx == ImageFBAttachmentStatus::invalidIdx || frameBufferAttachments[physicalResourceAttachmentIdx].viewType
                                != attachment.viewType)
                            {
                                uint32_t idx = frameBufferAttachmentDescs.size();

                                FrameBufferAttachmentDescriptor desc;
                                desc.idx = idx;
                                desc.format = m_LogicalResourceList[resource.idx].info.format;
                                desc.initLayout = physicalResourceLayouts[physicalResourceIdx].Query(attachment.range.imageRange);

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

                                physicalResourceLayouts[physicalResourceIdx].Update(attachment.range.imageRange, desc.finalLayout);

                                frameBufferAttachmentDescs.push_back(desc);
                                physicalResourceAttachmentTable[physicalResourceIdx].Update(attachment.range.imageRange, idx);


                                RenderGraphPassInfo::FBAttachment fbAttachment;
                                fbAttachment.assign = m_LogicalResourceAssignmentTable[resource.idx];
                                fbAttachment.subresource = attachment.range.imageRange;
                                fbAttachment.viewType = attachment.viewType;

                                frameBufferAttachments.push_back(fbAttachment);
                            }
                            // althrough logical resource has not been assigned, physical resource has been assigned
                            // assign the physical resource's frame buffer index to logical resource
                            else
                            {
                                // make a initial guess according to attachment information
                                RenderPassAttachmentOperationState opState;
                                // one resource should not be cleared twice

                                renderPassNode->pass->GetAttachmentOperationState(attachment, opState);
                                FrameBufferAttachmentDescriptor& desc = frameBufferAttachmentDescs[physicalResourceAttachmentIdx];
                                desc.finalLayout = renderPassNode->pass->GetAttachmentExpectedState(attachment);

                                // overwrite the clear color value requirment
                                // this branch might not be reached
                                if (desc.loadOp != VK_ATTACHMENT_LOAD_OP_CLEAR && opState.load == VK_ATTACHMENT_LOAD_OP_CLEAR)
                                {
                                    vkrg_assert(false);
                                    desc.loadOp = opState.load;
                                    desc.storeOp = opState.store;
                                    desc.stencilLoadOp = opState.stencilLoad;
                                    desc.stencilStoreOp = opState.stencilStore;

                                    VkClearValue clearValue;
                                    renderPassNode->pass->GetClearColor(attachment, clearValue);
                                    frameBufferAttachmentClearColor[desc.idx] = clearValue;
                                }

                                physicalResourceLayouts[physicalResourceIdx].Update(attachment.range.imageRange, desc.finalLayout);
                            }
                        }
                    }

                    // tranverse every attachment, collect all buffer barriers
                    for (uint32_t attachmentIdx = 0; attachmentIdx != attachments.size(); attachmentIdx++)
                    {
                        auto& resource = resources[attachmentIdx];
                        auto& attachment = attachments[attachmentIdx];

                        if (!attachment.IsBuffer()) continue;

                        VkBufferMemoryBarrier barrier{};
                        barrier.offset = attachment.range.bufferRange.offset;
                        barrier.size = attachment.range.bufferRange.size;
                        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                        barrier.pNext = NULL;
                        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

                        ResourceAssignment assign;
                        assign = m_LogicalResourceAssignmentTable[resource.idx];

                        RenderGraphBarrier::Handle handle;
                        handle.idx = assign.idx;
                        handle.external = assign.external;

                        if (attachment.type == RenderPassAttachment::BufferStorageInput || attachment.type == RenderPassAttachment::BufferInput)
                        {
                            VkPipelineStageFlagBits srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

                            auto& writer = m_LogicalResourceIODenpendencies[resource.idx].resourceWriteList[0];
                            if (m_RenderPassList[writer].pass->GetType() == RenderPassType::Compute)
                            {
                                srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
                            }
                            else if (m_RenderPassList[writer].pass->GetType() == RenderPassType::Graphics)
                            {
                                srcStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
                            }
                            else if (m_RenderPassList[writer].pass->GetType() == RenderPassType::Raytracing)
                            {
                                srcStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
                            }

                            // TODO better stage flag bit
                            barrierHelper.AddBuffer(srcStage, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, barrier, handle);
                        }
                    }
                }

                info.render.fbAttachmentIdx = frameBufferAttachments;
                info.render.expectedExtension = currentMergedPass->expectedExtension;

                // add frame buffer attachments
                for (uint32_t i = 0; i < frameBufferAttachmentDescs.size(); i++)
                {
                    auto& desc = frameBufferAttachmentDescs[i];
                    auto& fbAttachmentAssign = frameBufferAttachments[i].assign;


                    //http://geekfaner.com/shineengine/blog18_Vulkanv1.2_4.html
                    //如果一个render pass使用多个attachment alias相同的device memory，则每个attachment都必须包含 VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT 。attachments alias相同内存有如下几种方式：

                    //  多个attachment在创建framebuffer的时候，对应同一个image view
                    //  多个attachment对应同一个image的同一个image subresource的不同image view
                    //  多个attachment对应views of distinct image subresources bound to 覆盖的内存范围
                    VkAttachmentDescriptionFlags flags = 0;


                    // 检查当前render pass的attachment之间是否有重叠
                    // 若是则额外给attachment加 VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT
                    if (fbAttachmentAssign.external)
                    {
                        flags |= externalResourceAttachmentTable[fbAttachmentAssign.idx].MayAlias() ? VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT : 0;
                    }
                    else
                    {
                        flags |= physicalResourceAttachmentTable[fbAttachmentAssign.idx].MayAlias() ? VK_ATTACHMENT_DESCRIPTION_MAY_ALIAS_BIT : 0;
                    }


                    vkRenderPassCreateInfo.AddAttachment(
                        flags, desc.format,
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
                for (auto renderPassNode : currentMergedPass->renderPasses)
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
                        else if (dependingPassRpType == RenderPassType::Raytracing)
                        {
                            dependingMergedPassNodeStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
                            dependingMemoryAccessFlag = VK_ACCESS_MEMORY_WRITE_BIT;
                        }

                        VkPipelineStageFlags currentMergedPassNodeStage = 0;
                        VkAccessFlags currentMemoryAccessFlag = 0;

                        RenderPassType currentPassRpType = renderPassNode->pass->GetType();
                        {
                            currentMergedPassNodeStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                            currentMemoryAccessFlag = VK_ACCESS_MEMORY_READ_BIT;
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

                        uint32_t fbAttachmentIdx = ImageFBAttachmentStatus::invalidIdx;
                        if (!resource.external)
                        {
                            fbAttachmentIdx = physicalResourceAttachmentTable[m_LogicalResourceAssignmentTable[resource.idx].idx].Query(attachment.range.imageRange);
                        }
                        else
                        {
                            fbAttachmentIdx = externalResourceAttachmentTable[m_LogicalResourceAssignmentTable[resource.idx].idx].Query(attachment.range.imageRange);
                        }

                        // something goes wrong in our code if this operation fails
                        vkrg_assert(fbAttachmentIdx != ImageFBAttachmentStatus::invalidIdx);

                        RenderPassAttachmentOperationState opState{};
                        renderPassNode->pass->GetAttachmentOperationState(attachment, opState);

                        vkrg_assert(attachment.type != RenderPassAttachment::ImageStorageInput && attachment.type != RenderPassAttachment::ImageStorageOutput);

                        if (attachment.type == RenderPassAttachment::ImageColorInput)
                        {
                            vkRenderPassCreateInfo.AddSubpassInputAttachment(currentSubpassIndex, fbAttachmentIdx, renderPassNode->pass->GetAttachmentExpectedState(attachment));
                        }
                        else if (attachment.type == RenderPassAttachment::ImageColorOutput)
                        {
                            vkRenderPassCreateInfo.AddSubpassColorAttachment(currentSubpassIndex, fbAttachmentIdx);
                        }
                        else if (attachment.type == RenderPassAttachment::ImageDepthOutput || attachment.type == RenderPassAttachment::ImageDepthInput)
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
            }

            info.targetMergedPassIdx = currentMergedPass.GetId();

            if (info.type != RenderPassType::Graphics)
            {
                info.compute.barriers = barrierHelper.barriers;
            }
            else
            {
                info.render.bufferBarriers = barrierHelper.barriers;
            }

            m_renderGraphPassInfo.push_back(info);
        }

        m_finalGlobalBarriers = globalBarrierHelper.barriers;

        return RenderGraphCompileState::Success;
    }

    GvkImageCreateInfo RenderGraph::CreateImageCreateInfo(ResourceInfo info)
    {
        GvkImageCreateInfo imageCI{};

        auto [w, h, d] = GetExpectedExtension(info.ext, info.extType);

        imageCI.extent.width = w;
        imageCI.extent.height = h;
        imageCI.extent.depth = d;

        imageCI.arrayLayers = info.channelCount;
        // TODO add flags
        // 去除所有vkrg定义的额外flag
        imageCI.flags = info.extraFlags & ~((uint32_t)ResourceExtraFlag::AllExtraFlags);
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

        imageCI.tiling = m_DefaultImageTiling;
        imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCI.mipLevels = info.mipCount;
        imageCI.usage = info.usages;
        imageCI.samples = VK_SAMPLE_COUNT_1_BIT;

        return imageCI;
    }

    void RenderGraph::ResizePhysicalResources()
    {
        for (uint32_t physicalResourceIdx = 0; physicalResourceIdx != m_PhysicalResources.size(); physicalResourceIdx++)
        {
            auto& binding = m_PhysicalResourceBindings[physicalResourceIdx];
            const auto& info = m_PhysicalResources[physicalResourceIdx].info;

            binding.dirtyFlag = true;
            for (uint32_t i = 0; i < m_Options.flightFrameCount; i++)
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

                    if (m_Options.setDebugName)
                    {
                        binding.buffers[i]->SetDebugName("Physical_Resource_" + std::to_string(physicalResourceIdx));
                    }
                }
                else if (info.IsImage())
                {
                    if (binding.images[i] != nullptr) continue;

                    auto imageCI = CreateImageCreateInfo(info);
                    auto res = m_vulkanContext.ctx->CreateImage(imageCI);

                    // this operation shouldn't fail
                    // 2 cases might cause failure
                    // 1. some thing goes wrong with our validation checker
                    // 2. out of memory
                    vkrg_assert(res.has_value());
                    binding.images[i] = res.value();

                    if (m_Options.setDebugName)
                    {
                        binding.images[i]->SetDebugName("Physical_Resource_" + std::to_string(physicalResourceIdx));
                    }
                }
                else
                {
                    // this branch should not be reached
                    vkrg_assert(false);
                }

                if (m_Options.disableFrameOnFlight) break;
            }
        }

    }

    void RenderGraph::UpdateDirtyViews()
    {
        std::unordered_set<uint32_t> dirtyExternalResourceSet;
        std::unordered_set<uint32_t> dirtyPhysicalResourceSet;


        for (uint32_t i = 0; i < m_ExternalResourceBindings.size(); i++)
        {
            if (m_ExternalResourceBindings[i].dirtyFlag)
            {
                dirtyExternalResourceSet.insert(i);
                // we clear resource binding dirty flag in ResetResourceBindingFlag()
                // m_ExternalResourceBindings[i].dirtyFlag = false;
            }
        }
        for (uint32_t i = 0; i < m_PhysicalResourceBindings.size(); i++)
        {
            if (m_PhysicalResourceBindings[i].dirtyFlag)
            {
                dirtyPhysicalResourceSet.insert(i);
                // m_PhysicalResourceBindings[i].dirtyFlag = false;
            }
        }

        for (uint32_t passIdx = 0; passIdx < m_RenderPassList.size(); passIdx++)
        {
            auto& attachments = m_RenderPassList[passIdx].pass->GetAttachments();
            auto& resources = m_RenderPassList[passIdx].pass->GetAttachedResourceHandles();

            for (uint32_t attachmentIdx = 0; attachmentIdx < attachments.size(); attachmentIdx++)
            {
                auto& attachment = attachments[attachmentIdx];
                auto& resource = resources[attachmentIdx];
                auto& info = m_LogicalResourceList[resource.idx].info;

                ResourceBindingInfo* binding = NULL;

                // check if the corresponding resource is dirty
                bool viewRecreationRequired = false;
                if (resource.external)
                {
                    viewRecreationRequired = dirtyExternalResourceSet.count(m_LogicalResourceAssignmentTable[resource.idx].idx);
                    binding = &m_ExternalResourceBindings[m_LogicalResourceAssignmentTable[resource.idx].idx];
                }
                else
                {
                    viewRecreationRequired = dirtyPhysicalResourceSet.count(m_LogicalResourceAssignmentTable[resource.idx].idx);
                    binding = &m_PhysicalResourceBindings[m_LogicalResourceAssignmentTable[resource.idx].idx];
                }

                // recreate this view for every frame on flight
                if (viewRecreationRequired)
                {

                    for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
                    {
                        if (attachment.IsBuffer())
                        {
                            uint32_t targetBufferIndex = GetResourceFrameIdx(frameIdx, resource.external);

                            RenderPassViewTable::View view;
                            view.bufferView.buffer = binding->buffers[targetBufferIndex]->GetBuffer();
                            view.bufferView.size = attachment.range.bufferRange.size;
                            view.bufferView.offset = attachment.range.bufferRange.offset;

                            view.isImage = false;

                            m_RPViewTable[passIdx].attachmentViews[frameIdx][attachmentIdx] = view;
                        }
                        else if (attachment.IsImage())
                        {
                            uint32_t targetImageIndex = GetResourceFrameIdx(frameIdx, resource.external);

                            auto imageView = binding->images[targetImageIndex]->CreateView(attachment.range.imageRange.aspectMask,
                                attachment.range.imageRange.baseMipLevel,
                                attachment.range.imageRange.levelCount,
                                attachment.range.imageRange.baseArrayLayer,
                                attachment.range.imageRange.layerCount,
                                attachment.viewType);
                            vkrg_assert(imageView.has_value());

                            RenderPassViewTable::View view;
                            view.imageView = imageView.value();
                            view.isImage = true;

                            m_RPViewTable[passIdx].attachmentViews[frameIdx][attachmentIdx] = view;
                        }
                    }
                }
            }
        }

    }

    void RenderGraph::UpdateDirtyFrameBuffersAndBarriers()
    {
        // TODO update frame buffer according to view update
        for (auto& finalBarrier : m_finalGlobalBarriers)
        {
            for (uint32_t i = 0; i != finalBarrier.imageBarriers[0].size(); i++)
            {
                ResourceBindingInfo* binding = NULL;
                if (finalBarrier.imageBarrierHandles[i].external)
                {
                    binding = &m_ExternalResourceBindings[finalBarrier.imageBarrierHandles[i].idx];
                }
                else
                {
                    binding = &m_PhysicalResourceBindings[finalBarrier.imageBarrierHandles[i].idx];
                }

                if (binding->dirtyFlag)
                {
                    for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
                    {
                        uint32_t targetImageIdx = GetResourceFrameIdx(frameIdx, finalBarrier.imageBarrierHandles[i].external);
                        finalBarrier.imageBarriers[frameIdx][i].image = binding->images[targetImageIdx]->GetImage();
                    }
                }
            }
            for (uint32_t i = 0; i < finalBarrier.bufferBarriers[0].size(); i++)
            {
                ResourceBindingInfo* binding = NULL;
                if (finalBarrier.bufferBarrierHandles[i].external)
                {
                    binding = &m_ExternalResourceBindings[finalBarrier.bufferBarrierHandles[i].idx];
                }
                else
                {
                    binding = &m_PhysicalResourceBindings[finalBarrier.bufferBarrierHandles[i].idx];
                }

                if (binding->dirtyFlag)
                {
                    for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
                    {
                        uint32_t targetBufferIdx = GetResourceFrameIdx(frameIdx, finalBarrier.bufferBarrierHandles[i].external);
                        finalBarrier.bufferBarriers[frameIdx][i].buffer = binding->buffers[targetBufferIdx]->GetBuffer();
                    }
                }
            }
        }

        for (uint32_t renderPassIdx = 0; renderPassIdx < m_renderGraphPassInfo.size(); renderPassIdx++)
        {
            auto& passInfo = m_renderGraphPassInfo[renderPassIdx];
            auto& rpfBuffer = m_RPFrameBuffers[renderPassIdx];
            if (passInfo.IsGeneralPass())
            {
                for (auto& barrier : passInfo.compute.barriers)
                {
                    for (uint32_t i = 0; i < barrier.bufferBarriers[0].size(); i++)
                    {
                        ResourceBindingInfo* binding = NULL;
                        if (barrier.bufferBarrierHandles[i].external)
                        {
                            binding = &m_ExternalResourceBindings[barrier.bufferBarrierHandles[i].idx];
                        }
                        else
                        {
                            binding = &m_PhysicalResourceBindings[barrier.bufferBarrierHandles[i].idx];
                        }

                        if (binding->dirtyFlag)
                        {
                            for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
                            {
                                uint32_t targetBufferIdx = GetResourceFrameIdx(frameIdx, barrier.bufferBarrierHandles[i].external);
                                barrier.bufferBarriers[frameIdx][i].buffer = binding->buffers[targetBufferIdx]->GetBuffer();
                            }
                        }
                    }
                    for (uint32_t i = 0; i < barrier.imageBarriers[0].size(); i++)
                    {
                        ResourceBindingInfo* binding = NULL;
                        if (barrier.imageBarrierHandles[i].external)
                        {
                            binding = &m_ExternalResourceBindings[barrier.imageBarrierHandles[i].idx];
                        }
                        else
                        {
                            binding = &m_PhysicalResourceBindings[barrier.imageBarrierHandles[i].idx];
                        }

                        if (binding->dirtyFlag)
                        {
                            for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
                            {
                                uint32_t targetImageIdx = GetResourceFrameIdx(frameIdx, barrier.imageBarrierHandles[i].external);
                                barrier.imageBarriers[frameIdx][i].image = binding->images[targetImageIdx]->GetImage();
                            }
                        }
                    }
                }
            }
            else
            {
                bool frameBufferRecreate = false;
                for (uint32_t attachmentIdx = 0; attachmentIdx < passInfo.render.fbAttachmentIdx.size(); attachmentIdx++)
                {
                    auto& attachment = passInfo.render.fbAttachmentIdx[attachmentIdx];

                    ResourceBindingInfo* binding = GetAssignedResourceBinding(attachment.assign);

                    if (binding->dirtyFlag)
                    {
                        frameBufferRecreate = true;
                        break;
                    }
                }

                if (frameBufferRecreate)
                {
                    RenderPassExtension& ext = m_renderGraphPassInfo[renderPassIdx].render.expectedExtension;
                    for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
                    {
                        auto& attachments = m_renderGraphPassInfo[renderPassIdx].render.fbAttachmentIdx;

                        for (uint32_t attachmentIdx = 0; attachmentIdx < attachments.size(); attachmentIdx++)
                        {
                            ResourceBindingInfo* binding = GetAssignedResourceBinding(attachments[attachmentIdx].assign);

                            uint32_t targetFrameIdx = frameIdx;
                            if (!attachments[attachmentIdx].assign.external && m_Options.disableFrameOnFlight)
                            {
                                targetFrameIdx = 0;
                            }

                            auto& subresource = attachments[attachmentIdx].subresource;

                            // views will be cached and used so just create a new view is enough
                            auto view = binding->images[targetFrameIdx]->CreateView(subresource.aspectMask,
                                subresource.baseMipLevel,
                                subresource.levelCount,
                                subresource.baseArrayLayer,
                                subresource.layerCount,
                                attachments[attachmentIdx].viewType);
                            vkrg_assert(view.has_value());

                            m_RPFrameBuffers[renderPassIdx].frameBufferViews[frameIdx][attachmentIdx] = view.value();
                        }

                        if (m_RPFrameBuffers[renderPassIdx].frameBuffer[frameIdx] != NULL)
                        {
                            m_vulkanContext.ctx->DestroyFrameBuffer(m_RPFrameBuffers[renderPassIdx].frameBuffer[frameIdx]);
                        }

                        auto [w, h, d] = GetExpectedExtension(ext.extension, ext.extensionType);


                        auto fb = m_vulkanContext.ctx->CreateFrameBuffer(m_renderGraphPassInfo[renderPassIdx].render.renderPass,
                            m_RPFrameBuffers[renderPassIdx].frameBufferViews[frameIdx].data(),
                            w, h, d);
                        vkrg_assert(fb.has_value());

                        m_RPFrameBuffers[renderPassIdx].frameBuffer[frameIdx] = fb.value();
                    }
                }

                for (auto& barrier : passInfo.render.bufferBarriers)
                {
                    for (uint32_t i = 0; i < barrier.bufferBarriers[0].size(); i++)
                    {
                        ResourceBindingInfo* binding = NULL;
                        if (barrier.bufferBarrierHandles[i].external)
                        {
                            binding = &m_ExternalResourceBindings[barrier.bufferBarrierHandles[i].idx];
                        }
                        else
                        {
                            binding = &m_PhysicalResourceBindings[barrier.bufferBarrierHandles[i].idx];
                        }

                        if (binding->dirtyFlag)
                        {
                            for (uint32_t frameIdx = 0; frameIdx < m_Options.flightFrameCount; frameIdx++)
                            {
                                uint32_t targetBufferIdx = GetResourceFrameIdx(frameIdx, barrier.bufferBarrierHandles[i].external);
                                barrier.bufferBarriers[frameIdx][i].buffer = binding->buffers[targetBufferIdx]->GetBuffer();
                            }
                        }
                    }
                }
            }
        }

    }

    void RenderGraph::ResetResourceBindingDirtyFlag()
    {
        for (uint32_t i = 0; i < m_ExternalResourceBindings.size(); i++)
        {
            m_ExternalResourceBindings[i].dirtyFlag = false;
        }
        for (uint32_t i = 0; i < m_PhysicalResourceBindings.size(); i++)
        {
            m_PhysicalResourceBindings[i].dirtyFlag = false;
        }

    }

    void RenderGraph::GenerateCommands(VkCommandBuffer cmd, uint32_t frameIdx)
    {
        // TODO main body of excution
        for (uint32_t passIdx = 0; passIdx < m_renderGraphPassInfo.size(); passIdx++)
        {
            if (m_renderGraphPassInfo[passIdx].IsGraphicsPass())
            {
                auto& renderData = m_renderGraphPassInfo[passIdx].render;
                VkFramebuffer frameBuffer = m_RPFrameBuffers[passIdx].frameBuffer[frameIdx];

                auto [w, h, _] = GetExpectedExtension(renderData.expectedExtension.extension, renderData.expectedExtension.extensionType);

                auto& graphicsBarrier = m_renderGraphPassInfo[passIdx].render.bufferBarriers;
                for (uint32_t barrierIdx = 0; barrierIdx < graphicsBarrier.size(); barrierIdx++)
                {
                    auto& barrier = graphicsBarrier[barrierIdx];
                    vkrg_assert(barrier.imageBarriers[frameIdx].empty());

                    vkCmdPipelineBarrier(cmd, barrier.srcStage, barrier.dstStage,
                        0, 0, NULL, barrier.bufferBarriers[frameIdx].size(), barrier.bufferBarriers[frameIdx].data(),
                        0, NULL);
                }

                VkRect2D fullScreen;
                fullScreen.extent.width = w;
                fullScreen.extent.height = h;
                fullScreen.offset.x = 0;
                fullScreen.offset.y = 0;

                VkViewport vp;
                vp.height = h;
                vp.width = w;
                vp.x = 0;
                vp.y = 0;
                vp.minDepth = 0;
                vp.maxDepth = 1;

                if (renderData.mergedSubpassIndices.size() == 1)
                {
                    renderData.renderPass->Begin(frameBuffer, renderData.fbClearValues.data(),
                        fullScreen, vp, fullScreen, cmd).Record(
                            [&]()
                            {
                                uint32_t rpIdx = renderData.mergedSubpassIndices[0];
                    RenderPassRuntimeContext ctx(this, frameIdx, rpIdx);
                    m_RenderPassList[rpIdx].pass->OnRender(ctx, cmd);
                            }
                    );
                }
                else
                {
                    auto& renderPassInlineCtx = renderData.renderPass->Begin(frameBuffer, renderData.fbClearValues.data(),
                        fullScreen, vp, fullScreen, cmd);

                    for (uint32_t i = 0; i < renderData.mergedSubpassIndices.size(); i++)
                    {
                        auto operation = [&]()
                        {
                            uint32_t rpIdx = renderData.mergedSubpassIndices[i];
                            RenderPassRuntimeContext ctx(this, frameIdx, rpIdx);

                            m_RenderPassList[rpIdx].pass->OnRender(ctx, cmd);
                        };

                        if (i == renderData.mergedSubpassIndices.size() - 1)
                        {
                            renderPassInlineCtx.EndPass(operation);
                        }
                        else
                        {
                            renderPassInlineCtx.NextSubPass(operation);
                        }
                    }
                }
            }
            else
            {
                auto& computeData = m_renderGraphPassInfo[passIdx].compute;
                for (uint32_t barrierIdx = 0; barrierIdx < computeData.barriers.size(); barrierIdx++)
                {
                    auto& barrier = computeData.barriers[barrierIdx];
                    vkCmdPipelineBarrier(cmd, barrier.srcStage, barrier.dstStage,
                        0, 0, NULL, barrier.bufferBarriers[frameIdx].size(), barrier.bufferBarriers[frameIdx].data(),
                        barrier.imageBarriers[frameIdx].size(), barrier.imageBarriers[frameIdx].data());
                }

                uint32_t rpIdx = computeData.targetRenderPass;
                RenderPassRuntimeContext ctx(this, frameIdx, rpIdx);
                m_RenderPassList[rpIdx].pass->OnRender(ctx, cmd);
            }
        }

        if (!m_finalGlobalBarriers.empty())
        {
            for (uint32_t barrierIdx = 0; barrierIdx < m_finalGlobalBarriers.size(); barrierIdx++)
            {
                auto& barrier = m_finalGlobalBarriers[barrierIdx];
                vkCmdPipelineBarrier(cmd, barrier.srcStage, barrier.dstStage,
                    0, 0, NULL, barrier.bufferBarriers[frameIdx].size(), barrier.bufferBarriers[frameIdx].data(),
                    barrier.imageBarriers[frameIdx].size(), barrier.imageBarriers[frameIdx].data());
            }
        }


    }

    void RenderGraph::InitializeRPFrameBufferTable()
    {
        m_RPFrameBuffers.resize(m_renderGraphPassInfo.size());
        for (uint32_t i = 0; i < m_renderGraphPassInfo.size(); i++)
        {
            if (m_renderGraphPassInfo[i].IsGraphicsPass())
            {
                for (uint32_t fi = 0; fi < m_Options.flightFrameCount; fi++)
                {
                    m_RPFrameBuffers[i].frameBufferViews[fi].resize(m_renderGraphPassInfo[i].render.fbAttachmentIdx.size());
                }
            }
        }
    }

    void RenderGraph::InitializeRenderPassViewTable()
    {
        m_RPViewTable.resize(m_RenderPassList.size());
        for (uint32_t passIdx = 0; passIdx < m_RenderPassList.size(); passIdx++)
        {
            for (uint32_t frameIdx = 0; frameIdx < maxFrameOnFlightCount; frameIdx++)
            {
                m_RPViewTable[passIdx].attachmentViews[frameIdx].resize(m_RenderPassList[passIdx].pass->GetAttachments().size());
            }
        }
    }

    void RenderGraph::ClearCompileCache()
    {
        // m_Graph.Clear();
        // m_MergedRenderPassGraph.Clear();
        // m_RenderPassNodeList.clear();
        m_LogicalResourceIODenpendencies.clear();
        // m_MergedRenderPasses.clear();
    }

    void RenderGraph::PostCompile()
    {
        m_PhysicalResourceBindings.resize(m_PhysicalResources.size());
        m_ExternalResourceBindings.resize(m_ExternalResources.size());

        InitializeRPFrameBufferTable();
        InitializeRenderPassViewTable();
        ResizePhysicalResources();
        ClearCompileCache();
    }

    uint32_t RenderGraph::GetResourceFrameIdx(uint32_t idx, bool external)
    {
        if (!external && m_Options.disableFrameOnFlight)
        {
            return 0;
        }
        return idx;
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
        pass.idx = m_MergedRenderPassGraph.NodeCount();
        pass.renderPasses.push_back(node);
        pass.canBeMerged = mergable;
        pass.expectedExtension = node->pass->GetRenderPassExtension();

        DAGMergedNode currentMergedNode = m_MergedRenderPassGraph.AddNode(pass);

        vkrg_assert(pass.idx == currentMergedNode.GetId());
        return currentMergedNode;
    }

    opt<tpl<RenderGraph::DAGMergedNode, uint32_t>> RenderGraph::FindInvolvedMergedPass(DAGNode node)
    {
        /*
        for (uint32_t i = 0;i < m_MergedRenderPasses.size();i++)
        {
            //for (auto renderPassNode : (*m_MergedRenderPasses[i]).renderPasses)
            for(uint32_t subpassIdx = 0; subpassIdx < (*m_MergedRenderPasses[i]).renderPasses.size(); subpassIdx++)
            {
                auto renderPassNode = (*m_MergedRenderPasses[i]).renderPasses[subpassIdx];
                if (renderPassNode == node)
                {
                    return std::make_tuple(m_MergedRenderPasses[i], subpassIdx);
                }
            }
        }
        */

        for (auto mergedPassIter = m_MergedRenderPassGraph.Begin(); mergedPassIter != m_MergedRenderPassGraph.End(); mergedPassIter++)
        {
            //for (auto renderPassNode : (*m_MergedRenderPasses[i]).renderPasses)
            for (uint32_t subpassIdx = 0; subpassIdx < mergedPassIter->renderPasses.size(); subpassIdx++)
            {
                auto renderPassNode = mergedPassIter->renderPasses[subpassIdx];
                if (renderPassNode == node)
                {
                    return std::make_tuple(mergedPassIter, subpassIdx);
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

    RenderGraph::DAGMergedNode RenderGraph::FindFirstAccessedNodeForResource(uint32_t logicalResourceIdx)
    {
        auto& dependency = m_LogicalResourceIODenpendencies[logicalResourceIdx];

        DAGMergedNode node;
        if (auto tmp = FindInvolvedMergedPass(m_RenderPassNodeList[dependency.resourceWriteList[0]]); tmp.has_value())
        {
            auto [rv, _] = tmp.value();
            node = rv;
        }

        return node;
    }

    tpl<uint32_t, uint32_t, uint32_t> RenderGraph::GetExpectedExtension(ResourceInfo::Extension ext, ResourceExtensionType type)
    {

        uint32_t w, h, d;
        if (type == ResourceExtensionType::Screen)
        {
            // scale the image by its screen size
            w = (uint32_t)(ext.screen.x * (float)m_Options.screenWidth);
            h = (uint32_t)(ext.screen.y * (float)m_Options.screenHeight);
            d = 1;
        }
        else if (type == ResourceExtensionType::Fixed)
        {
            w = ext.fixed.x;
            h = ext.fixed.y;
            d = ext.fixed.z;
        }
        else
        {
            vkrg_assert(false);
        }


        return std::make_tuple(w, h, d);
    }

    uint32_t RenderGraph::GetRenderGraphPassInfoIndex(DAGMergedNode node)
    {
        for (uint32_t i = 0; i < m_renderGraphPassInfo.size(); i++)
        {
            if (m_renderGraphPassInfo[i].targetMergedPassIdx == node.GetId())
            {
                return i;
            }
        }

        return m_renderGraphPassInfo.size();
    }

    static bool Contains(uint32_t src, uint32_t target)
    {
        return (src & target) == target;
    }

    static bool HasImageCompatibleUsages(VkFormatFeatureFlags features, VkImageUsageFlags flags)
    {
        if (Contains(flags, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) && !Contains(features, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
        {
            return false;
        }
        if (Contains(flags, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) && !Contains(features, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            return false;
        }
        if (Contains(flags, VK_IMAGE_USAGE_SAMPLED_BIT) && !Contains(features, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        {
            return true;
        }
        if (Contains(flags, VK_IMAGE_USAGE_TRANSFER_SRC_BIT) && !Contains(features, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        {
            return false;
        }
        if (Contains(flags, VK_IMAGE_USAGE_TRANSFER_DST_BIT) && !Contains(features, VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
        {
            return false;
        }
        if (Contains(flags, VK_IMAGE_USAGE_STORAGE_BIT) && !Contains(features, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        {
            return false;
        }

        return true;
    }

    static bool HasBufferCompatibleUsages(VkFormatFeatureFlags features, VkBufferUsageFlags usages)
    {
        if (Contains(usages, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) && !Contains(features, VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))
        {
            return false;
        }
        if (Contains(usages, VK_BUFFER_USAGE_TRANSFER_SRC_BIT) && !Contains(features, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT))
        {
            return false;
        }
        if (Contains(usages, VK_BUFFER_USAGE_TRANSFER_DST_BIT) && !Contains(features, VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
        {
            return false;
        }
        return true;
    }


    bool RenderGraph::CheckImageUsageCompability(VkFormat format, VkImageUsageFlags usage)
    {
        // we only support formats in range 0~supportedFormatCount 
        if (format > ResourceFormatCompabilityCache::supportedFormatCount)
        {
            return false;
        }

        VkFormatProperties properties;
        if (m_FormatCompabilityCache.initialized[format])
        {
            properties = m_FormatCompabilityCache.formatProperties[format];
        }
        else
        {
            vkGetPhysicalDeviceFormatProperties(m_vulkanContext.ctx->GetPhysicalDevice(), format, &properties);

            m_FormatCompabilityCache.initialized[format] = true;
            m_FormatCompabilityCache.formatProperties[format] = properties;
        }

        if constexpr (m_DefaultImageTiling == VK_IMAGE_TILING_LINEAR)
        {
            return HasImageCompatibleUsages(properties.linearTilingFeatures, usage);
        }
        else if constexpr (m_DefaultImageTiling == VK_IMAGE_TILING_OPTIMAL)
        {
            return HasImageCompatibleUsages(properties.optimalTilingFeatures, usage);
        }

        return false;
    }

    bool RenderGraph::CheckBufferUsageCompability(VkFormat format, VkBufferUsageFlags usage)
    {
        if (format == VK_FORMAT_UNDEFINED) return true;

        if (format > ResourceFormatCompabilityCache::supportedFormatCount)
        {
            return false;
        }

        VkFormatProperties properties;
        if (m_FormatCompabilityCache.initialized[format])
        {
            properties = m_FormatCompabilityCache.formatProperties[format];
        }
        else
        {
            vkGetPhysicalDeviceFormatProperties(m_vulkanContext.ctx->GetPhysicalDevice(), format, &properties);

            m_FormatCompabilityCache.initialized[format] = true;
            m_FormatCompabilityCache.formatProperties[format] = properties;
        }

        return HasBufferCompatibleUsages(properties.bufferFeatures, usage);
    }

    RenderGraph::ResourceBindingInfo* RenderGraph::GetAssignedResourceBinding(ResourceAssignment assign)
    {
        if (assign.external)
        {
            return &m_ExternalResourceBindings[assign.idx];
        }
        else
        {
            return &m_PhysicalResourceBindings[assign.idx];
        }

        return nullptr;
    }

    bool RenderGraph::SubresourceCompability(RenderPassAttachment& lhs, RenderPassAttachment& rhs)
    {
        if (lhs.type != rhs.type) return false;

        if (lhs.IsImage())
        {
            return lhs.range.imageRange.aspectMask == rhs.range.imageRange.aspectMask &&
                lhs.range.imageRange.baseArrayLayer == rhs.range.imageRange.baseArrayLayer &&
                lhs.range.imageRange.baseMipLevel == rhs.range.imageRange.baseMipLevel &&
                lhs.range.imageRange.layerCount == rhs.range.imageRange.layerCount &&
                lhs.range.imageRange.levelCount == rhs.range.imageRange.levelCount;
        }
        else if (lhs.IsBuffer())
        {
            return lhs.range.bufferRange.offset == rhs.range.bufferRange.offset &&
                lhs.range.bufferRange.size == rhs.range.bufferRange.size;
        }

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
        m_Graph->m_ExternalResourceBindings[assign.idx].dirtyFlag = true;

        return true;
    }

    bool RenderGraphDataFrame::BindImage(const char* name, uint32_t frameIdx, ptr<gvk::Image> image)
    {
        vkrg_assert(m_Graph->m_HaveCompiled);
        if (frameIdx >= m_FlightFrameCount) return false;

        RenderGraph::ResourceAssignment assign;
        auto resOpt = m_Graph->FindGraphResource(name);

        if (resOpt.has_value() && m_Graph->m_LogicalResourceList[resOpt.value().idx].info.IsImage())
        {
            assign = m_Graph->m_LogicalResourceAssignmentTable[resOpt.value().idx];
        }
        else
        {
            return false;
        }

        if (assign.Invalid() || (assign.external && m_Target != External) || (!assign.external && m_Target != Physical)) return false;

        GvkImageCreateInfo info = image->Info();
        // check image info's compatibilty
        GvkImageCreateInfo expectedInfo = m_Graph->CreateImageCreateInfo(m_Graph->m_LogicalResourceList[resOpt.value().idx].info);

        if (info.arrayLayers != expectedInfo.arrayLayers ||
            info.extent.width != expectedInfo.extent.width ||
            info.extent.height != expectedInfo.extent.height ||
            info.flags != expectedInfo.flags ||
            info.format != expectedInfo.format ||
            info.imageType != expectedInfo.imageType ||
            info.mipLevels != expectedInfo.mipLevels ||
            info.samples != expectedInfo.samples ||
            (expectedInfo.usage & info.usage) != expectedInfo.usage ||
            expectedInfo.tiling != info.tiling)
        {
            return false;
        }

        m_Graph->m_ExternalResourceBindings[assign.idx].images[frameIdx] = image;
        m_Graph->m_ExternalResourceBindings[assign.idx].dirtyFlag = true;
        return true;
    }

    RenderGraphDataFrame::RenderGraphDataFrame(RenderGraph* graph, Target target)
        :m_Graph(graph), m_Target(target)
    {
        m_FlightFrameCount = m_Graph->m_Options.flightFrameCount;
    }


    bool RenderGraph::RenderGraphPassInfo::IsGeneralPass()
    {
        return type != RenderPassType::Graphics;
    }

    bool RenderGraph::RenderGraphPassInfo::IsGraphicsPass()
    {
        return type == RenderPassType::Graphics;
    }

}
