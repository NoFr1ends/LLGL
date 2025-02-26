/*
 * MTRenderSystem.mm
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2018 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "MTRenderSystem.h"
#include "../CheckedCast.h"
#include "../TextureUtils.h"
#include "../../Core/Helper.h"
#include "../../Core/Vendor.h"
#include "MTFeatureSet.h"
#include <LLGL/ImageFlags.h>
#include <AvailabilityMacros.h>


namespace LLGL
{


/* ----- Common ----- */

MTRenderSystem::MTRenderSystem()
{
    CreateDeviceResources();
    QueryRenderingCaps();
}

MTRenderSystem::~MTRenderSystem()
{
    [device_ release];
}

/* ----- Render Context ----- */

RenderContext* MTRenderSystem::CreateRenderContext(const RenderContextDescriptor& desc, const std::shared_ptr<Surface>& surface)
{
    return TakeOwnership(renderContexts_, MakeUnique<MTRenderContext>(device_, desc, surface));
}

void MTRenderSystem::Release(RenderContext& renderContext)
{
    RemoveFromUniqueSet(renderContexts_, &renderContext);
}

/* ----- Command queues ----- */

CommandQueue* MTRenderSystem::GetCommandQueue()
{
    return commandQueue_.get();
}

/* ----- Command buffers ----- */

CommandBuffer* MTRenderSystem::CreateCommandBuffer(const CommandBufferDescriptor& /*desc*/)
{
    return TakeOwnership(commandBuffers_, MakeUnique<MTCommandBuffer>(device_, commandQueue_->GetNative()));
}

void MTRenderSystem::Release(CommandBuffer& commandBuffer)
{
    RemoveFromUniqueSet(commandBuffers_, &commandBuffer);
}

/* ----- Buffers ------ */

Buffer* MTRenderSystem::CreateBuffer(const BufferDescriptor& desc, const void* initialData)
{
    return TakeOwnership(buffers_, MakeUnique<MTBuffer>(device_, desc, initialData));
}

BufferArray* MTRenderSystem::CreateBufferArray(std::uint32_t numBuffers, Buffer* const * bufferArray)
{
    AssertCreateBufferArray(numBuffers, bufferArray);
    return TakeOwnership(bufferArrays_, MakeUnique<MTBufferArray>(bufferArray[0]->GetBindFlags(), numBuffers, bufferArray));
}

void MTRenderSystem::Release(Buffer& buffer)
{
    RemoveFromUniqueSet(buffers_, &buffer);
}

void MTRenderSystem::Release(BufferArray& bufferArray)
{
    RemoveFromUniqueSet(bufferArrays_, &bufferArray);
}

void MTRenderSystem::WriteBuffer(Buffer& dstBuffer, std::uint64_t dstOffset, const void* data, std::uint64_t dataSize)
{
    auto& dstBufferMT = LLGL_CAST(MTBuffer&, dstBuffer);
    dstBufferMT.Write(static_cast<NSUInteger>(dstOffset), data, static_cast<NSUInteger>(dataSize));
}

void* MTRenderSystem::MapBuffer(Buffer& buffer, const CPUAccess access)
{
    auto& bufferMT = LLGL_CAST(MTBuffer&, buffer);
    return bufferMT.Map(access);
}

void MTRenderSystem::UnmapBuffer(Buffer& buffer)
{
    auto& bufferMT = LLGL_CAST(MTBuffer&, buffer);
    return bufferMT.Unmap();
}

/* ----- Textures ----- */

Texture* MTRenderSystem::CreateTexture(const TextureDescriptor& textureDesc, const SrcImageDescriptor* imageDesc)
{
    auto textureMT = MakeUnique<MTTexture>(device_, textureDesc);

    if (imageDesc)
    {
        textureMT->Write(
            //TextureRegion{ Offset3D{ 0, 0, 0 }, textureMT->GetMipExtent(0) },
            TextureRegion
            {
                TextureSubresource{ 0, textureDesc.arrayLayers, 0, 1 },
                Offset3D{ 0, 0, 0 },
                textureDesc.extent
            },
            *imageDesc
        );

        /* Generate MIP-maps if enabled */
        if (MustGenerateMipsOnCreate(textureDesc))
        {
            id<MTLCommandBuffer> cmdBuffer = [commandQueue_->GetNative() commandBuffer];
            {
                id<MTLBlitCommandEncoder> blitCmdEncoder = [cmdBuffer blitCommandEncoder];
                [blitCmdEncoder generateMipmapsForTexture:textureMT->GetNative()];
                [blitCmdEncoder endEncoding];
            }
            [cmdBuffer commit];
        }
    }

    return TakeOwnership(textures_, std::move(textureMT));
}

void MTRenderSystem::Release(Texture& texture)
{
    RemoveFromUniqueSet(textures_, &texture);
}

void MTRenderSystem::WriteTexture(Texture& texture, const TextureRegion& textureRegion, const SrcImageDescriptor& imageDesc)
{
    auto& textureMT = LLGL_CAST(MTTexture&, texture);
    textureMT.Write(textureRegion, imageDesc);
}

void MTRenderSystem::ReadTexture(const Texture& texture, std::uint32_t mipLevel, const DstImageDescriptor& imageDesc)
{
    //todo
}

/* ----- Sampler States ---- */

Sampler* MTRenderSystem::CreateSampler(const SamplerDescriptor& desc)
{
    return TakeOwnership(samplers_, MakeUnique<MTSampler>(device_, desc));
}

void MTRenderSystem::Release(Sampler& sampler)
{
    RemoveFromUniqueSet(samplers_, &sampler);
}

/* ----- Resource Heaps ----- */

ResourceHeap* MTRenderSystem::CreateResourceHeap(const ResourceHeapDescriptor& desc)
{
    return TakeOwnership(resourceHeaps_, MakeUnique<MTResourceHeap>(desc));
}

void MTRenderSystem::Release(ResourceHeap& resourceHeap)
{
    RemoveFromUniqueSet(resourceHeaps_, &resourceHeap);
}

/* ----- Render Passes ----- */

RenderPass* MTRenderSystem::CreateRenderPass(const RenderPassDescriptor& desc)
{
    AssertCreateRenderPass(desc);
    return TakeOwnership(renderPasses_, MakeUnique<MTRenderPass>(desc));
}

void MTRenderSystem::Release(RenderPass& renderPass)
{
    RemoveFromUniqueSet(renderPasses_, &renderPass);
}

/* ----- Render Targets ----- */

RenderTarget* MTRenderSystem::CreateRenderTarget(const RenderTargetDescriptor& desc)
{
    return TakeOwnership(renderTargets_, MakeUnique<MTRenderTarget>(device_, desc));
}

void MTRenderSystem::Release(RenderTarget& renderTarget)
{
    RemoveFromUniqueSet(renderTargets_, &renderTarget);
}

/* ----- Shader ----- */

Shader* MTRenderSystem::CreateShader(const ShaderDescriptor& desc)
{
    AssertCreateShader(desc);
    return TakeOwnership(shaders_, MakeUnique<MTShader>(device_, desc));
}

ShaderProgram* MTRenderSystem::CreateShaderProgram(const ShaderProgramDescriptor& desc)
{
    AssertCreateShaderProgram(desc);
    return TakeOwnership(shaderPrograms_, MakeUnique<MTShaderProgram>(device_, desc));
}

void MTRenderSystem::Release(Shader& shader)
{
    RemoveFromUniqueSet(shaders_, &shader);
}

void MTRenderSystem::Release(ShaderProgram& shaderProgram)
{
    RemoveFromUniqueSet(shaderPrograms_, &shaderProgram);
}

/* ----- Pipeline Layouts ----- */

PipelineLayout* MTRenderSystem::CreatePipelineLayout(const PipelineLayoutDescriptor& desc)
{
    return TakeOwnership(pipelineLayouts_, MakeUnique<MTPipelineLayout>(desc));
}

void MTRenderSystem::Release(PipelineLayout& pipelineLayout)
{
    RemoveFromUniqueSet(pipelineLayouts_, &pipelineLayout);
}

/* ----- Pipeline States ----- */

GraphicsPipeline* MTRenderSystem::CreateGraphicsPipeline(const GraphicsPipelineDescriptor& desc)
{
    return TakeOwnership(graphicsPipelines_, MakeUnique<MTGraphicsPipeline>(device_, desc));
}

ComputePipeline* MTRenderSystem::CreateComputePipeline(const ComputePipelineDescriptor& desc)
{
    return TakeOwnership(computePipelines_, MakeUnique<MTComputePipeline>(device_, desc));
}

void MTRenderSystem::Release(GraphicsPipeline& graphicsPipeline)
{
    RemoveFromUniqueSet(graphicsPipelines_, &graphicsPipeline);
}

void MTRenderSystem::Release(ComputePipeline& computePipeline)
{
    RemoveFromUniqueSet(computePipelines_, &computePipeline);
}

/* ----- Queries ----- */

QueryHeap* MTRenderSystem::CreateQueryHeap(const QueryHeapDescriptor& desc)
{
    return nullptr;//todo
}

void MTRenderSystem::Release(QueryHeap& queryHeap)
{
    //todo
    //RemoveFromUniqueSet(queryHeaps_, &queryHeap);
}

/* ----- Fences ----- */

Fence* MTRenderSystem::CreateFence()
{
    return TakeOwnership(fences_, MakeUnique<MTFence>(device_));
}

void MTRenderSystem::Release(Fence& fence)
{
    RemoveFromUniqueSet(fences_, &fence);
}


/*
 * ======= Private: =======
 */

void MTRenderSystem::CreateDeviceResources()
{
    /* Create Metal device */
    device_ = MTLCreateSystemDefaultDevice();
    if (device_ == nil)
        throw std::runtime_error("failed to create Metal device");
    
    /* Initialize renderer information */
    RendererInfo info;
    {
        info.rendererName           = "Metal " + std::string(QueryMetalVersion());
        info.deviceName             = [[device_ name] cStringUsingEncoding:NSUTF8StringEncoding];
        info.vendorName             = "Apple";
        info.shadingLanguageName    = "Metal Shading Language";
    }
    SetRendererInfo(info);

    /* Create command queue */
    commandQueue_ = MakeUnique<MTCommandQueue>(device_);
}

void MTRenderSystem::QueryRenderingCaps()
{
    RenderingCapabilities caps;
    LoadFeatureSetCaps(device_, QueryHighestFeatureSet(), caps);
    SetRenderingCaps(caps);
}

const char* MTRenderSystem::QueryMetalVersion() const
{
    #if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11
    switch (QueryHighestFeatureSet())
    {
        #if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_14
        case MTLFeatureSet_macOS_GPUFamily2_v1: return "2.1";
        case MTLFeatureSet_macOS_GPUFamily1_v4: return "1.4";
        #endif
        #if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_13
        case MTLFeatureSet_macOS_GPUFamily1_v3: return "1.3";
        #endif
        #if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12
        case MTLFeatureSet_macOS_GPUFamily1_v2: return "1.2";
        #endif
        case MTLFeatureSet_macOS_GPUFamily1_v1: return "1.1";
        default:                                break;
    }
    #endif
    return "1.0";
}

MTLFeatureSet MTRenderSystem::QueryHighestFeatureSet() const
{
    static const MTLFeatureSet g_potentialFeatureSets[] =
    {
        #if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_14
        MTLFeatureSet_macOS_ReadWriteTextureTier2,
        MTLFeatureSet_macOS_GPUFamily2_v1,
        MTLFeatureSet_macOS_GPUFamily1_v4,
        #endif
        #if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_13
        MTLFeatureSet_macOS_GPUFamily1_v3,
        #endif
        #if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12
        MTLFeatureSet_macOS_GPUFamily1_v2,
        #endif
        MTLFeatureSet_macOS_GPUFamily1_v1,
    };

    for (auto fset : g_potentialFeatureSets)
    {
        if ([device_ supportsFeatureSet:fset])
            return fset;
    }

    return MTLFeatureSet_macOS_GPUFamily1_v1;
}


} // /namespace LLGL



// ================================================================================
