/*
 * RendererConfiguration.h
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2019 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#ifndef LLGL_RENDERER_CONFIGURATION_H
#define LLGL_RENDERER_CONFIGURATION_H


#include <string>
#include <vector>
#include <cstdint>


namespace LLGL
{


/* ----- Enumerations ----- */

/**
\brief OpenGL context profile enumeration.
\see RendererConfigurationOpenGL::contextProfile
*/
enum class OpenGLContextProfile
{
    //! OpenGL compatibility profile.
    CompatibilityProfile,

    //! OpenGL core profile.
    CoreProfile,

    /**
    \brief OpenGL ES profile.
    \todo Not supported yet
    */
    ESProfile,
};


/* ----- Structures ----- */

/**
\brief Application descriptor structure.
\note Only supported with: Vulkan.
\see RendererConfigurationVulkan::application
*/
struct ApplicationDescriptor
{
    //! Descriptive string of the application.
    std::string     applicationName;

    //! Version number of the application.
    std::uint32_t   applicationVersion;

    //! Descriptive string of the engine or middleware.
    std::string     engineName;

    //! Version number of the engine or middleware.
    std::uint32_t   engineVersion;
};

/**
\brief Structure for a Vulkan renderer specific configuration.
\remarks The nomenclature here is "Renderer" instead of "RenderSystem" since the configuration is renderer specific
and does not denote a configuration of the entire system.
*/
struct RendererConfigurationVulkan
{
    /**
    \brief Application descriptor used when a Vulkan debug or validation layer is enabled.
    \see ApplicationDescriptor
    */
    ApplicationDescriptor       application;

    /**
    \brief List of Vulkan layers to enable. The ones that are not supported, will be ignored.
    \remarks For example, the layer \c "VK_LAYER_KHRONOS_validation" can be used for a stronger validation.
    */
    std::vector<std::string>    enabledLayers;

    /**
    \brief Minimal allocation size for a device memory chunk. By default 1024*1024, i.e. 1 MB of VRAM.
    \remarks Vulkan only allows a limited set of device memory objects (e.g. 4096 on a GPU with 8 GB of VRAM).
    This member specifies the minimum size used for hardware memory allocation of such a memory chunk.
    The Vulkan render system automatically manages sub-region allocation and defragmentation.
    \todo Remove this as soon as Vulkan memory manage has been improved.
    */
    std::uint64_t               minDeviceMemoryAllocationSize   = 1024*1024;

    /**
    \brief Specifies whether fragmentation of the device memory blocks shall be kept low. By default false.
    \remarks If this is true, each buffer and image allocation first tries to find a reusable device memory block
    within a single VkDeviceMemory chunk (which might be potentially slower).
    Whenever a VkDeviceMemory chunk is full, the memory manager tries to reduce fragmentation anyways.
    \todo Remove this as soon as Vulkan memory manage has been improved.
    */
    bool                        reduceDeviceMemoryFragmentation = false;
};

/**
\brief OpenGL profile descriptor structure.
\note On MacOS the only supported OpenGL profiles are compatibility profile (for lagecy OpenGL before 3.0), 3.2 core profile, or 4.1 core profile.
*/
struct RendererConfigurationOpenGL
{
    //! Specifies the requested OpenGL context profile. By default OpenGLContextProfile::CoreProfile.
    OpenGLContextProfile    contextProfile  = OpenGLContextProfile::CoreProfile;

    /**
    \brief Specifies the requested OpenGL context major version. By default 0.
    \remarks If both \c majorVersion and \c minorVersion are 0, the highest OpenGL version that is availabel on the host system will be choosen.
    \remarks This member is ignored if \c contextProfile is OpenGLContextProfile::CompatibilityProfile.
    */
    int                     majorVersion    = 0;

    /**
    \brief Specifies the requested OpenGL context minor version. By default 0.
    \remarks If both \c majorVersion and \c minorVersion are 0, the highest OpenGL version that is availabel on the host system will be choosen.
    \remarks This member is ignored if \c contextProfile is OpenGLContextProfile::CompatibilityProfile.
    */
    int                     minorVersion    = 0;
};


} // /namespace LLGL


#endif



// ================================================================================
