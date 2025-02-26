/*
 * GLRenderSystem.cpp
 *
 * This file is part of the "LLGL" project (Copyright (c) 2015-2019 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "GLRenderSystem.h"
#include "Texture/GLMipGenerator.h"
#include "Ext/GLExtensions.h"
#include "RenderState/GLStatePool.h"
#include "../RenderSystemUtils.h"
#include "../GLCommon/GLTypes.h"
#include "../GLCommon/GLCore.h"
#include "../GLCommon/Texture/GLTexImage.h"
#include "../GLCommon/Texture/GLTexSubImage.h"
#include "Buffer/GLBufferWithVAO.h"
#include "Buffer/GLBufferArrayWithVAO.h"
#include "../CheckedCast.h"
#include "../TextureUtils.h"
#include "../../Core/Helper.h"
#include "../../Core/Assertion.h"
#include "GLRenderingCaps.h"
#include "Command/GLImmediateCommandBuffer.h"
#include "Command/GLDeferredCommandBuffer.h"


namespace LLGL
{


/* ----- Common ----- */

GLRenderSystem::GLRenderSystem(const RenderSystemDescriptor& renderSystemDesc)
{
    /* Extract optional renderer configuartion */
    if (auto rendererConfigGL = GetRendererConfiguration<RendererConfigurationOpenGL>(renderSystemDesc))
        config_ = *rendererConfigGL;
}

GLRenderSystem::~GLRenderSystem()
{
    /* Clear all render state containers first, the rest will be deleted automatically */
    GLMipGenerator::Get().Clear();
    GLStatePool::Get().Clear();
}

/* ----- Render Context ----- */

// private
GLRenderContext* GLRenderSystem::GetSharedRenderContext() const
{
    return (!renderContexts_.empty() ? renderContexts_.begin()->get() : nullptr);
}

RenderContext* GLRenderSystem::CreateRenderContext(const RenderContextDescriptor& desc, const std::shared_ptr<Surface>& surface)
{
    return AddRenderContext(MakeUnique<GLRenderContext>(desc, config_, surface, GetSharedRenderContext()));
}

void GLRenderSystem::Release(RenderContext& renderContext)
{
    RemoveFromUniqueSet(renderContexts_, &renderContext);
}

/* ----- Command queues ----- */

CommandQueue* GLRenderSystem::GetCommandQueue()
{
    return commandQueue_.get();
}

/* ----- Command buffers ----- */

CommandBuffer* GLRenderSystem::CreateCommandBuffer(const CommandBufferDescriptor& desc)
{
    /* Get state manager from shared render context */
    if (auto sharedContext = GetSharedRenderContext())
    {
        if ((desc.flags & (CommandBufferFlags::DeferredSubmit | CommandBufferFlags::MultiSubmit)) != 0)
        {
            /* Create deferred command buffer */
            return TakeOwnership(
                commandBuffers_,
                MakeUnique<GLDeferredCommandBuffer>(desc.flags)
            );
        }
        else
        {
            /* Create immediate command buffer */
            return TakeOwnership(
                commandBuffers_,
                MakeUnique<GLImmediateCommandBuffer>(sharedContext->GetStateManager())
            );
        }
    }
    else
        throw std::runtime_error("cannot create OpenGL command buffer without active render context");
}

void GLRenderSystem::Release(CommandBuffer& commandBuffer)
{
    RemoveFromUniqueSet(commandBuffers_, &commandBuffer);
}

/* ----- Buffers ------ */

static GLbitfield GetGLBufferStorageFlags(long cpuAccessFlags)
{
    #ifdef GL_ARB_buffer_storage

    GLbitfield flagsGL = 0;

    /* Allways enable dynamic storage, to enable usage of 'glBufferSubData' */
    flagsGL |= GL_DYNAMIC_STORAGE_BIT;

    if ((cpuAccessFlags & CPUAccessFlags::Read) != 0)
        flagsGL |= GL_MAP_READ_BIT;
    if ((cpuAccessFlags & CPUAccessFlags::Write) != 0)
        flagsGL |= GL_MAP_WRITE_BIT;

    return flagsGL;

    #else

    return 0;

    #endif // /GL_ARB_buffer_storage
}

static GLenum GetGLBufferUsage(long miscFlags)
{
    return ((miscFlags & MiscFlags::DynamicUsage) != 0 ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
}

static void GLBufferStorage(GLBuffer& bufferGL, const BufferDescriptor& desc, const void* initialData)
{
    bufferGL.BufferStorage(
        static_cast<GLsizeiptr>(desc.size),
        initialData,
        GetGLBufferStorageFlags(desc.cpuAccessFlags),
        GetGLBufferUsage(desc.miscFlags)
    );
}

Buffer* GLRenderSystem::CreateBuffer(const BufferDescriptor& desc, const void* initialData)
{
    AssertCreateBuffer(desc, static_cast<std::uint64_t>(std::numeric_limits<GLsizeiptr>::max()));

    auto bufferGL = CreateGLBuffer(desc, initialData);

    /* Store meta data for certain types of buffers */
    if ((desc.bindFlags & BindFlags::IndexBuffer) != 0 && desc.indexFormat != Format::Undefined)
        bufferGL->SetIndexType(desc.indexFormat);

    return bufferGL;
}

// private
GLBuffer* GLRenderSystem::CreateGLBuffer(const BufferDescriptor& desc, const void* initialData)
{
    /* Create either base of sub-class GLBuffer object */
    if ((desc.bindFlags & BindFlags::VertexBuffer) != 0)
    {
        /* Create buffer with VAO and build vertex array */
        auto bufferGL = MakeUnique<GLBufferWithVAO>(desc.bindFlags);
        {
            GLBufferStorage(*bufferGL, desc, initialData);
            bufferGL->BuildVertexArray(desc.vertexAttribs.size(), desc.vertexAttribs.data());
        }
        return TakeOwnership(buffers_, std::move(bufferGL));
    }
    else
    {
        /* Create generic buffer */
        auto bufferGL = MakeUnique<GLBuffer>(desc.bindFlags);
        {
            GLBufferStorage(*bufferGL, desc, initialData);
        }
        return TakeOwnership(buffers_, std::move(bufferGL));
    }
}

BufferArray* GLRenderSystem::CreateBufferArray(std::uint32_t numBuffers, Buffer* const * bufferArray)
{
    AssertCreateBufferArray(numBuffers, bufferArray);

    auto refBindFlags = bufferArray[0]->GetBindFlags();
    if ((refBindFlags & BindFlags::VertexBuffer) != 0)
    {
        /* Create vertex buffer array and build VAO */
        auto vertexBufferArray = MakeUnique<GLBufferArrayWithVAO>(refBindFlags);
        vertexBufferArray->BuildVertexArray(numBuffers, bufferArray);
        return TakeOwnership(bufferArrays_, std::move(vertexBufferArray));
    }

    return TakeOwnership(bufferArrays_, MakeUnique<GLBufferArray>(refBindFlags, numBuffers, bufferArray));
}

void GLRenderSystem::Release(Buffer& buffer)
{
    RemoveFromUniqueSet(buffers_, &buffer);
}

void GLRenderSystem::Release(BufferArray& bufferArray)
{
    RemoveFromUniqueSet(bufferArrays_, &bufferArray);
}

void GLRenderSystem::WriteBuffer(Buffer& dstBuffer, std::uint64_t dstOffset, const void* data, std::uint64_t dataSize)
{
    auto& dstBufferGL = LLGL_CAST(GLBuffer&, dstBuffer);
    dstBufferGL.BufferSubData(static_cast<GLintptr>(dstOffset), static_cast<GLsizeiptr>(dataSize), data);
}

void* GLRenderSystem::MapBuffer(Buffer& buffer, const CPUAccess access)
{
    auto& bufferGL = LLGL_CAST(GLBuffer&, buffer);
    return bufferGL.MapBuffer(GLTypes::Map(access));
}

void GLRenderSystem::UnmapBuffer(Buffer& buffer)
{
    auto& bufferGL = LLGL_CAST(GLBuffer&, buffer);
    bufferGL.UnmapBuffer();
}

/* ----- Textures ----- */

static GLint GetGlTextureMinFilter(const TextureDescriptor& textureDesc)
{
    if (IsMipMappedTexture(textureDesc))
        return GL_LINEAR_MIPMAP_LINEAR;
    else
        return GL_LINEAR;
}

Texture* GLRenderSystem::CreateTexture(const TextureDescriptor& textureDesc, const SrcImageDescriptor* imageDesc)
{
    auto texture = MakeUnique<GLTexture>(textureDesc);

    /* Bind texture */
    GLStateManager::Get().BindGLTexture(*texture);

    /* Initialize texture parameters for the first time */
    auto target = GLTypes::Map(textureDesc.type);

    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GetGlTextureMinFilter(textureDesc));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    #if 0//TODO
    /* Configure texture swizzling if format is not supported */
    const auto& formatDesc = GetFormatAttribs(textureDesc.format);
    if (formatDesc.format == ImageFormat::Alpha)
    {
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, GL_ZERO);
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, GL_ZERO);
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, GL_ZERO);
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, GL_RED);
    }
    else if (formatDesc.format == ImageFormat::BGRA)
    {
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);
    }
    #endif

    /* Build texture storage and upload image dataa */
    switch (textureDesc.type)
    {
        case TextureType::Texture1D:
            GLTexImage1D(textureDesc, imageDesc);
            break;

        case TextureType::Texture2D:
            GLTexImage2D(textureDesc, imageDesc);
            break;

        case TextureType::Texture3D:
            LLGL_ASSERT_FEATURE_SUPPORT(has3DTextures);
            GLTexImage3D(textureDesc, imageDesc);
            break;

        case TextureType::TextureCube:
            LLGL_ASSERT_FEATURE_SUPPORT(hasCubeTextures);
            GLTexImageCube(textureDesc, imageDesc);
            break;

        case TextureType::Texture1DArray:
            LLGL_ASSERT_FEATURE_SUPPORT(hasArrayTextures);
            GLTexImage1DArray(textureDesc, imageDesc);
            break;

        case TextureType::Texture2DArray:
            LLGL_ASSERT_FEATURE_SUPPORT(hasArrayTextures);
            GLTexImage2DArray(textureDesc, imageDesc);
            break;

        case TextureType::TextureCubeArray:
            LLGL_ASSERT_FEATURE_SUPPORT(hasCubeArrayTextures);
            GLTexImageCubeArray(textureDesc, imageDesc);
            break;

        case TextureType::Texture2DMS:
            LLGL_ASSERT_FEATURE_SUPPORT(hasMultiSampleTextures);
            GLTexImage2DMS(textureDesc);
            break;

        case TextureType::Texture2DMSArray:
            LLGL_ASSERT_FEATURE_SUPPORT(hasMultiSampleTextures);
            GLTexImage2DMSArray(textureDesc);
            break;

        default:
            throw std::invalid_argument("failed to create texture with invalid texture type");
            break;
    }

    /* Generate MIP-maps if enabled */
    if (imageDesc != nullptr && MustGenerateMipsOnCreate(textureDesc))
        GLMipGenerator::Get().GenerateMips(textureDesc.type);

    return TakeOwnership(textures_, std::move(texture));
}

#if 0//TODO
Texture* GLRenderSystem::CreateTextureView(Texture& sharedTexture, const TextureViewDescriptor& textureViewDesc)
{
    LLGL_ASSERT_FEATURE_SUPPORT(hasTextureViews);

    auto& sharedTextureGL = LLGL_CAST(GLTexture&, sharedTexture);
    auto texture = MakeUnique<GLTexture>(textureViewDesc.type);

    /* Initialize texture as texture-view */
    texture->TextureView(sharedTextureGL, textureViewDesc);

    /* Initialize texture swizzle (if specified) */
    if (!IsTextureSwizzleIdentity(textureViewDesc.swizzle))
    {
        /* Bind texture */
        GLStateManager::Get().BindGLTexture(*texture);

        /* Initialize texture parameters for the first time */
        auto target = GLTypes::Map(textureViewDesc.type);

        glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, GLTypes::Map(textureViewDesc.swizzle.r));
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, GLTypes::Map(textureViewDesc.swizzle.g));
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, GLTypes::Map(textureViewDesc.swizzle.b));
        glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, GLTypes::Map(textureViewDesc.swizzle.a));
    }

    return TakeOwnership(textures_, std::move(texture));
}
#endif

void GLRenderSystem::Release(Texture& texture)
{
    RemoveFromUniqueSet(textures_, &texture);
}

/* ----- "WriteTexture..." functions ----- */

void GLRenderSystem::WriteTexture(Texture& texture, const TextureRegion& textureRegion, const SrcImageDescriptor& imageDesc)
{
    /* Bind texture and write texture sub data */
    auto& textureGL = LLGL_CAST(GLTexture&, texture);
    GLStateManager::Get().BindGLTexture(textureGL);

    /* Write data into specific texture type */
    switch (texture.GetType())
    {
        case TextureType::Texture1D:
            GLTexSubImage1D(textureRegion, imageDesc);
            break;

        case TextureType::Texture2D:
            GLTexSubImage2D(textureRegion, imageDesc);
            break;

        case TextureType::Texture3D:
            LLGL_ASSERT_FEATURE_SUPPORT(has3DTextures);
            GLTexSubImage3D(textureRegion, imageDesc);
            break;

        case TextureType::TextureCube:
            LLGL_ASSERT_FEATURE_SUPPORT(hasCubeTextures);
            GLTexSubImageCube(textureRegion, imageDesc);
            break;

        case TextureType::Texture1DArray:
            LLGL_ASSERT_FEATURE_SUPPORT(hasArrayTextures);
            GLTexSubImage1DArray(textureRegion, imageDesc);
            break;

        case TextureType::Texture2DArray:
            LLGL_ASSERT_FEATURE_SUPPORT(hasArrayTextures);
            GLTexSubImage2DArray(textureRegion, imageDesc);
            break;

        case TextureType::TextureCubeArray:
            LLGL_ASSERT_FEATURE_SUPPORT(hasCubeArrayTextures);
            GLTexSubImageCubeArray(textureRegion, imageDesc);
            break;

        default:
            break;
    }
}

void GLRenderSystem::ReadTexture(const Texture& texture, std::uint32_t mipLevel, const DstImageDescriptor& imageDesc)
{
    LLGL_ASSERT_PTR(imageDesc.data);

    auto& textureGL = LLGL_CAST(const GLTexture&, texture);

    /* Read image data from texture */
    #if defined GL_ARB_direct_state_access && defined LLGL_GL_ENABLE_DSA_EXT
    if (HasExtension(GLExt::ARB_direct_state_access))
    {
        glGetTextureImage(
            textureGL.GetID(),
            static_cast<GLint>(mipLevel),
            GLTypes::Map(imageDesc.format),
            GLTypes::Map(imageDesc.dataType),
            static_cast<GLsizei>(imageDesc.dataSize),
            imageDesc.data
        );
    }
    else
    #endif
    {
        /* Bind texture and read image data from texture */
        GLStateManager::Get().BindGLTexture(textureGL);
        glGetTexImage(
            GLTypes::Map(textureGL.GetType()),
            static_cast<GLint>(mipLevel),
            GLTypes::Map(imageDesc.format),
            GLTypes::Map(imageDesc.dataType),
            imageDesc.data
        );
    }
}

/* ----- Sampler States ---- */

Sampler* GLRenderSystem::CreateSampler(const SamplerDescriptor& desc)
{
    LLGL_ASSERT_FEATURE_SUPPORT(hasSamplers);
    auto sampler = MakeUnique<GLSampler>();
    sampler->SetDesc(desc);
    return TakeOwnership(samplers_, std::move(sampler));
}

void GLRenderSystem::Release(Sampler& sampler)
{
    RemoveFromUniqueSet(samplers_, &sampler);
}

/* ----- Resource Heaps ----- */

ResourceHeap* GLRenderSystem::CreateResourceHeap(const ResourceHeapDescriptor& desc)
{
    return TakeOwnership(resourceHeaps_, MakeUnique<GLResourceHeap>(desc));
}

void GLRenderSystem::Release(ResourceHeap& resourceHeap)
{
    RemoveFromUniqueSet(resourceHeaps_, &resourceHeap);
}

/* ----- Render Passes ----- */

RenderPass* GLRenderSystem::CreateRenderPass(const RenderPassDescriptor& desc)
{
    AssertCreateRenderPass(desc);
    return TakeOwnership(renderPasses_, MakeUnique<GLRenderPass>(desc));
}

void GLRenderSystem::Release(RenderPass& renderPass)
{
    RemoveFromUniqueSet(renderPasses_, &renderPass);
}

/* ----- Render Targets ----- */

RenderTarget* GLRenderSystem::CreateRenderTarget(const RenderTargetDescriptor& desc)
{
    LLGL_ASSERT_FEATURE_SUPPORT(hasRenderTargets);
    AssertCreateRenderTarget(desc);
    return TakeOwnership(renderTargets_, MakeUnique<GLRenderTarget>(desc));
}

void GLRenderSystem::Release(RenderTarget& renderTarget)
{
    RemoveFromUniqueSet(renderTargets_, &renderTarget);
}

/* ----- Shader ----- */

Shader* GLRenderSystem::CreateShader(const ShaderDescriptor& desc)
{
    AssertCreateShader(desc);

    /* Validate rendering capabilities for required shader type */
    switch (desc.type)
    {
        case ShaderType::Geometry:
            LLGL_ASSERT_FEATURE_SUPPORT(hasGeometryShaders);
            break;
        case ShaderType::TessControl:
        case ShaderType::TessEvaluation:
            LLGL_ASSERT_FEATURE_SUPPORT(hasTessellationShaders);
            break;
        case ShaderType::Compute:
            LLGL_ASSERT_FEATURE_SUPPORT(hasComputeShaders);
            break;
        default:
            break;
    }

    /* Make and return shader object */
    return TakeOwnership(shaders_, MakeUnique<GLShader>(desc));
}

ShaderProgram* GLRenderSystem::CreateShaderProgram(const ShaderProgramDescriptor& desc)
{
    AssertCreateShaderProgram(desc);
    return TakeOwnership(shaderPrograms_, MakeUnique<GLShaderProgram>(desc));
}

void GLRenderSystem::Release(Shader& shader)
{
    RemoveFromUniqueSet(shaders_, &shader);
}

void GLRenderSystem::Release(ShaderProgram& shaderProgram)
{
    RemoveFromUniqueSet(shaderPrograms_, &shaderProgram);
}

/* ----- Pipeline Layouts ----- */

PipelineLayout* GLRenderSystem::CreatePipelineLayout(const PipelineLayoutDescriptor& desc)
{
    return TakeOwnership(pipelineLayouts_, MakeUnique<GLPipelineLayout>(desc));
}

void GLRenderSystem::Release(PipelineLayout& pipelineLayout)
{
    RemoveFromUniqueSet(pipelineLayouts_, &pipelineLayout);
}

/* ----- Pipeline States ----- */

GraphicsPipeline* GLRenderSystem::CreateGraphicsPipeline(const GraphicsPipelineDescriptor& desc)
{
    return TakeOwnership(graphicsPipelines_, MakeUnique<GLGraphicsPipeline>(desc, GetRenderingCaps().limits));
}

ComputePipeline* GLRenderSystem::CreateComputePipeline(const ComputePipelineDescriptor& desc)
{
    return TakeOwnership(computePipelines_, MakeUnique<GLComputePipeline>(desc));
}

void GLRenderSystem::Release(GraphicsPipeline& graphicsPipeline)
{
    RemoveFromUniqueSet(graphicsPipelines_, &graphicsPipeline);
}

void GLRenderSystem::Release(ComputePipeline& computePipeline)
{
    RemoveFromUniqueSet(computePipelines_, &computePipeline);
}

/* ----- Queries ----- */

QueryHeap* GLRenderSystem::CreateQueryHeap(const QueryHeapDescriptor& desc)
{
    return TakeOwnership(queryHeaps_, MakeUnique<GLQueryHeap>(desc));
}

void GLRenderSystem::Release(QueryHeap& queryHeap)
{
    RemoveFromUniqueSet(queryHeaps_, &queryHeap);
}

/* ----- Fences ----- */

Fence* GLRenderSystem::CreateFence()
{
    return TakeOwnership(fences_, MakeUnique<GLFence>());
}

void GLRenderSystem::Release(Fence& fence)
{
    RemoveFromUniqueSet(fences_, &fence);
}


/*
 * ======= Protected: =======
 */

RenderContext* GLRenderSystem::AddRenderContext(std::unique_ptr<GLRenderContext>&& renderContext)
{
    /* Create devices that require an active GL context */
    if (renderContexts_.empty())
        CreateGLContextDependentDevices(*renderContext);

    /* Use uniform clipping space */
    GLStateManager::Get().DetermineExtensionsAndLimits();
    GLStateManager::Get().SetClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);

    /* Take ownership and return raw pointer */
    return TakeOwnership(renderContexts_, std::move(renderContext));
}


/*
 * ======= Private: =======
 */

void GLRenderSystem::CreateGLContextDependentDevices(GLRenderContext& renderContext)
{
    const bool hasGLCoreProfile = (config_.contextProfile == OpenGLContextProfile::CoreProfile);

    /* Load all OpenGL extensions */
    LoadGLExtensions(hasGLCoreProfile);

    /* Enable debug callback function */
    if (debugCallback_)
        SetDebugCallback(debugCallback_);

    /* Create command queue instance */
    commandQueue_ = MakeUnique<GLCommandQueue>(renderContext.GetStateManager());
}

void GLRenderSystem::LoadGLExtensions(bool hasGLCoreProfile)
{
    /* Load OpenGL extensions if not already done */
    if (!AreExtensionsLoaded())
    {
        /* Query extensions and load all of them */
        auto extensions = QueryExtensions(hasGLCoreProfile);
        LoadAllExtensions(extensions, hasGLCoreProfile);

        /* Query and store all renderer information and capabilities */
        QueryRendererInfo();
        QueryRenderingCaps();
    }
}

void APIENTRY GLDebugCallback(
    GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    /* Generate output stream */
    std::stringstream typeStr;

    typeStr
        << "OpenGL debug callback ("
        << GLDebugSourceToStr(source) << ", "
        << GLDebugTypeToStr(type) << ", "
        << GLDebugSeverityToStr(severity) << ")";

    /* Call debug callback */
    auto debugCallback = reinterpret_cast<const DebugCallback*>(userParam);
    (*debugCallback)(typeStr.str(), message);
}

void GLRenderSystem::SetDebugCallback(const DebugCallback& debugCallback)
{
    #ifdef GL_KHR_debug
    if (HasExtension(GLExt::KHR_debug))
    {
        debugCallback_ = debugCallback;
        if (debugCallback_)
        {
            GLStateManager::Get().Enable(GLState::DEBUG_OUTPUT);
            GLStateManager::Get().Enable(GLState::DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(GLDebugCallback, &debugCallback_);
        }
        else
        {
            GLStateManager::Get().Disable(GLState::DEBUG_OUTPUT);
            GLStateManager::Get().Disable(GLState::DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(nullptr, nullptr);
        }
    }
    #endif // /GL_KHR_debug
}

static std::string GLGetString(GLenum name)
{
    auto bytes = glGetString(name);
    return (bytes != nullptr ? std::string(reinterpret_cast<const char*>(bytes)) : "");
}

void GLRenderSystem::QueryRendererInfo()
{
    RendererInfo info;

    info.rendererName           = "OpenGL " + GLGetString(GL_VERSION);
    info.deviceName             = GLGetString(GL_RENDERER);
    info.vendorName             = GLGetString(GL_VENDOR);
    info.shadingLanguageName    = "GLSL " + GLGetString(GL_SHADING_LANGUAGE_VERSION);

    SetRendererInfo(info);
}

void GLRenderSystem::QueryRenderingCaps()
{
    RenderingCapabilities caps;
    GLQueryRenderingCaps(caps);
    SetRenderingCaps(caps);
}


} // /namespace LLGL



// ================================================================================
