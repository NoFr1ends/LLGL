/*
 * ExampleBase.cpp
 *
 * This file is part of the "LLGL" project (Copyright (c) 2015-2019 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include <ExampleBase.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>


/*
 * Global helper functions
 */

std::string GetSelectedRendererModule(int argc, char* argv[])
{
    /* Select renderer module */
    std::string rendererModule;

    if (argc > 1)
    {
        /* Get renderer module name from command line argument */
        rendererModule = argv[1];
    }
    else
    {
        /* Find available modules */
        auto modules = LLGL::RenderSystem::FindModules();

        if (modules.empty())
        {
            /* No modules available -> throw error */
            throw std::runtime_error("no renderer modules available on target platform");
        }
        else if (modules.size() == 1)
        {
            /* Use the only available module */
            rendererModule = modules.front();
        }
        else
        {
            /* Let user select a renderer */
            while (rendererModule.empty())
            {
                /* Print list of available modules */
                std::cout << "select renderer:" << std::endl;

                int i = 0;
                for (const auto& mod : modules)
                    std::cout << " " << (++i) << ".) " << mod << std::endl;

                /* Wait for user input */
                std::size_t selection = 0;
                std::cin >> selection;
                --selection;

                if (selection < modules.size())
                    rendererModule = modules[selection];
                else
                    std::cerr << "invalid input" << std::endl;
            }
        }
    }

    /* Choose final renderer module */
    std::cout << "selected renderer: " << rendererModule << std::endl;

    return rendererModule;
}

std::string ReadFileContent(const std::string& filename)
{
    // Read file content into string
    std::ifstream file(filename);

    if (!file.good())
        throw std::runtime_error("failed to open file: \"" + filename + "\"");

    return std::string(
        ( std::istreambuf_iterator<char>(file) ),
        ( std::istreambuf_iterator<char>() )
    );
}

std::vector<char> ReadFileBuffer(const std::string& filename)
{
    // Read file content into buffer
    std::ifstream file(filename, std::ios_base::binary | std::ios_base::ate);

    if (!file.good())
        throw std::runtime_error("failed to open file: \"" + filename + "\"");

    auto fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);

    return buffer;
}


/*
 * TutorialShaderDescriptor struct
 */

ExampleBase::TutorialShaderDescriptor::TutorialShaderDescriptor(
    LLGL::ShaderType    type,
    const std::string&  filename)
:
    type     { type     },
    filename { filename }
{
}

ExampleBase::TutorialShaderDescriptor::TutorialShaderDescriptor(
    LLGL::ShaderType    type,
    const std::string&  filename,
    const std::string&  entryPoint,
    const std::string&  profile)
:
    type       { type       },
    filename   { filename   },
    entryPoint { entryPoint },
    profile    { profile    }
{
}


/*
 * ResizeEventHandler class
 */

ExampleBase::ResizeEventHandler::ResizeEventHandler(ExampleBase& tutorial, LLGL::RenderContext* context, Gs::Matrix4f& projection) :
    tutorial_   { tutorial   },
    context_    { context    },
    projection_ { projection }
{
}

void ExampleBase::ResizeEventHandler::OnResize(LLGL::Window& sender, const LLGL::Extent2D& clientAreaSize)
{
    if (clientAreaSize.width >= 4 && clientAreaSize.height >= 4)
    {
        // Update video mode
        auto videoMode = context_->GetVideoMode();
        {
            videoMode.resolution = clientAreaSize;
        }
        context_->SetVideoMode(videoMode);

        // Update projection matrix
        auto aspectRatio = static_cast<float>(videoMode.resolution.width) / static_cast<float>(videoMode.resolution.height);
        projection_ = tutorial_.PerspectiveProjection(aspectRatio, 0.1f, 100.0f, Gs::Deg2Rad(45.0f));

        // Re-draw frame
        if (tutorial_.IsLoadingDone())
            tutorial_.OnDrawFrame();
    }
}

void ExampleBase::ResizeEventHandler::OnTimer(LLGL::Window& sender, std::uint32_t timerID)
{
    // Re-draw frame
    if (tutorial_.IsLoadingDone())
        tutorial_.OnDrawFrame();
}


/*
 * ExampleBase class
 */

std::string ExampleBase::rendererModule_;

void ExampleBase::SelectRendererModule(int argc, char* argv[])
{
    rendererModule_ = GetSelectedRendererModule(argc, argv);
}

void ExampleBase::Run()
{
    LLGL::Extent2D resolution = context->GetResolution();

    while (context->GetSurface().ProcessEvents() && !input->KeyDown(LLGL::Key::Escape))
    {
        // Update profiler
        profilerObj_->NextProfile();

        // Draw current frame
        OnDrawFrame();

        // Check if resolution has changed
        auto currentResolution = context->GetResolution();
        if (resolution != currentResolution)
        {
            OnResize(currentResolution);
            resolution = currentResolution;
        }
    }
}

ExampleBase::ExampleBase(
    const std::wstring&     title,
    const LLGL::Extent2D&   resolution,
    std::uint32_t           multiSampling,
    bool                    vsync,
    bool                    debugger)
:
    profilerObj_ { new LLGL::RenderingProfiler() },
    debuggerObj_ { new LLGL::RenderingDebugger() },
    timer        { LLGL::Timer::Create()         },
    profiler     { *profilerObj_                 }
{
    // Set report callback to standard output
    LLGL::Log::SetReportCallbackStd();
    LLGL::Log::SetReportLimit(10);

    // Set up renderer descriptor
    LLGL::RenderSystemDescriptor rendererDesc = rendererModule_;

    #if defined _DEBUG && 0
    rendererDesc.debugCallback = [](const std::string& type, const std::string& message)
    {
        std::cerr << type << ": " << message << std::endl;
    };
    #endif

    // Create render system
    renderer = LLGL::RenderSystem::Load(
        rendererDesc,
        #ifdef LLGL_DEBUG//_DEBUG
        (debugger ? profilerObj_.get() : nullptr),
        (debugger ? debuggerObj_.get() : nullptr)
        #else
        nullptr, nullptr
        #endif
    );

    // Create render context
    LLGL::RenderContextDescriptor contextDesc;
    {
        contextDesc.videoMode.resolution    = resolution;
        contextDesc.vsync.enabled           = vsync;
        contextDesc.multiSampling.enabled   = (multiSampling > 1);
        contextDesc.multiSampling.samples   = multiSampling;
    }
    context = renderer->CreateRenderContext(contextDesc);

    multiSampleDesc_ = contextDesc.multiSampling;

    // Create command buffer
    commands = renderer->CreateCommandBuffer();

    // Get command queue
    commandQueue = renderer->GetCommandQueue();

    // Initialize command buffer
    commands->SetClearColor(defaultClearColor);

    // Print renderer information
    const auto& info = renderer->GetRendererInfo();

    std::cout << "renderer information:" << std::endl;
    std::cout << "  renderer:         " << info.rendererName << std::endl;
    std::cout << "  device:           " << info.deviceName << std::endl;
    std::cout << "  vendor:           " << info.vendorName << std::endl;
    std::cout << "  shading language: " << info.shadingLanguageName << std::endl;

    #ifdef LLGL_MOBILE_PLATFORM

    // Set canvas title
    auto& canvas = LLGL::CastTo<LLGL::Canvas>(context->GetSurface());

    auto rendererName = renderer->GetName();
    canvas.SetTitle(title + L" ( " + std::wstring(rendererName.begin(), rendererName.end()) + L" )");

    #else // LLGL_MOBILE_PLATFORM

    // Set window title
    auto& window = LLGL::CastTo<LLGL::Window>(context->GetSurface());

    auto rendererName = renderer->GetName();
    window.SetTitle(title + L" ( " + std::wstring(rendererName.begin(), rendererName.end()) + L" )");

    // Add input event listener to window
    input = std::make_shared<LLGL::Input>();
    window.AddEventListener(input);

    // Change window descriptor to allow resizing
    auto wndDesc = window.GetDesc();
    wndDesc.resizable = true;
    window.SetDesc(wndDesc);

    // Change window behavior
    auto behavior = window.GetBehavior();
    behavior.disableClearOnResize = true;
    behavior.moveAndResizeTimerID = 1;
    window.SetBehavior(behavior);

    // Add window resize listener
    window.AddEventListener(std::make_shared<ResizeEventHandler>(*this, context, projection));

    // Show window
    window.Show();

    #endif // /LLGL_MOBILE_PLATFORM

    // Initialize default projection matrix
    projection = PerspectiveProjection(GetAspectRatio(), 0.1f, 100.0f, Gs::Deg2Rad(45.0f));

    // Store information that loading is done
    loadingDone_ = true;
}

void ExampleBase::OnResize(const LLGL::Extent2D& resoluion)
{
    // dummy
}

LLGL::ShaderProgram* ExampleBase::LoadShaderProgram(
    const std::vector<TutorialShaderDescriptor>&    shaderDescs,
    const std::vector<LLGL::VertexFormat>&          vertexFormats,
    const LLGL::VertexFormat&                       streamOutputFormat,
    const std::vector<LLGL::FragmentAttribute>&     fragmentAttribs)
{
    ShaderProgramRecall recall;

    recall.shaderDescs = shaderDescs;

    // Store vertex input attributes
    for (const auto& vtxFmt : vertexFormats)
    {
        recall.vertexAttribs.inputAttribs.insert(
            recall.vertexAttribs.inputAttribs.end(),
            vtxFmt.attributes.begin(),
            vtxFmt.attributes.end()
        );
    }

    // Store vertex output attributs
    recall.vertexAttribs.outputAttribs = streamOutputFormat.attributes;
    recall.fragmentAttribs.outputAttribs = fragmentAttribs;

    for (const auto& desc : shaderDescs)
    {
        // Create shader
        auto shaderDesc = LLGL::ShaderDescFromFile(desc.type, desc.filename.c_str(), desc.entryPoint.c_str(), desc.profile.c_str());
        {
            if (desc.type == LLGL::ShaderType::Vertex)
                shaderDesc.vertex = recall.vertexAttribs;
            else if (desc.type == LLGL::ShaderType::Fragment)
                shaderDesc.fragment = recall.fragmentAttribs;
        }
        auto shader = renderer->CreateShader(shaderDesc);

        // Print info log (warnings and errors)
        std::string log = shader->GetReport();
        if (!log.empty())
            std::cerr << log << std::endl;

        // Store shader in recall
        recall.shaders.push_back(shader);
    }

    // Create shader program
    auto shaderProgram = renderer->CreateShaderProgram(LLGL::ShaderProgramDesc(recall.shaders));

    // Link shader program and check for errors
    if (shaderProgram->HasErrors())
        throw std::runtime_error(shaderProgram->GetReport());

    // Store information in call
    shaderPrograms_[shaderProgram] = recall;

    return shaderProgram;
}

bool ExampleBase::ReloadShaderProgram(LLGL::ShaderProgram*& shaderProgram)
{
    if (!shaderProgram)
        return false;

    std::cout << "reload shader program" << std::endl;

    // Find shader program in the recall map
    auto it = shaderPrograms_.find(shaderProgram);
    if (it == shaderPrograms_.end())
        return false;

    auto& recall = it->second;
    std::vector<LLGL::Shader*> shaders;

    try
    {
        // Recompile all shaders
        for (const auto& desc : recall.shaderDescs)
        {
            // Read shader file
            auto shaderCode = ReadFileContent(desc.filename);

            // Create shader
            auto shaderDesc = LLGL::ShaderDescFromFile(desc.type, desc.filename.c_str(), desc.entryPoint.c_str(), desc.profile.c_str());
            {
                if (desc.type == LLGL::ShaderType::Vertex)
                    shaderDesc.vertex = recall.vertexAttribs;
                else if (desc.type == LLGL::ShaderType::Fragment)
                    shaderDesc.fragment = recall.fragmentAttribs;
            }
            auto shader = renderer->CreateShader(shaderDesc);

            // Print info log (warnings and errors)
            std::string log = shader->GetReport();
            if (!log.empty())
                std::cerr << log << std::endl;

            // Store new shader
            shaders.push_back(shader);
        }

        // Create new shader program
        auto newShaderProgram = renderer->CreateShaderProgram(LLGL::ShaderProgramDesc(shaders));

        // Link shader program and check for errors
        if (newShaderProgram->HasErrors())
        {
            // Print errors and release shader program
            std::cerr << newShaderProgram->GetReport() << std::endl;
            renderer->Release(*newShaderProgram);
        }
        else
        {
            // Delete all previous shaders
            for (auto shader : recall.shaders)
                renderer->Release(*shader);

            // Store new shaders in recall
            recall.shaders = std::move(shaders);

            // Delete old and use new shader program
            renderer->Release(*shaderProgram);
            shaderProgram = newShaderProgram;

            return true;
        }
    }
    catch (const std::exception& err)
    {
        // Print error message
        std::cerr << err.what() << std::endl;
    }

    return false;
}

LLGL::ShaderProgram* ExampleBase::LoadStandardShaderProgram(const std::vector<LLGL::VertexFormat>& vertexFormats)
{
    // Load shader program
    if (Supported(LLGL::ShadingLanguage::GLSL))
    {
        return LoadShaderProgram(
            {
                { LLGL::ShaderType::Vertex,   "Example.vert" },
                { LLGL::ShaderType::Fragment, "Example.frag" }
            },
            vertexFormats
        );
    }
    if (Supported(LLGL::ShadingLanguage::SPIRV))
    {
        return LoadShaderProgram(
            {
                { LLGL::ShaderType::Vertex,   "Example.450core.vert.spv" },
                { LLGL::ShaderType::Fragment, "Example.450core.frag.spv" }
            },
            vertexFormats
        );
    }
    if (Supported(LLGL::ShadingLanguage::HLSL))
    {
        return LoadShaderProgram(
            {
                { LLGL::ShaderType::Vertex,   "Example.hlsl", "VS", "vs_5_0" },
                { LLGL::ShaderType::Fragment, "Example.hlsl", "PS", "ps_5_0" }
            },
            vertexFormats
        );
    }
    if (Supported(LLGL::ShadingLanguage::Metal))
    {
        return LoadShaderProgram(
            {
                { LLGL::ShaderType::Vertex,   "Example.metal", "VS", "1.1" },
                { LLGL::ShaderType::Fragment, "Example.metal", "PS", "1.1" }
            },
            vertexFormats
        );
    }

    return nullptr;
}

LLGL::Texture* LoadTextureWithRenderer(LLGL::RenderSystem& renderSys, const std::string& filename, long bindFlags)
{
    // Load image data from file (using STBI library, see https://github.com/nothings/stb)
    int width = 0, height = 0, components = 0;

    auto imageBuffer = stbi_load(filename.c_str(), &width, &height, &components, 4);
    if (!imageBuffer)
        throw std::runtime_error("failed to load texture from file: \"" + filename + "\"");

    // Initialize source image descriptor to upload image data onto hardware texture
    LLGL::SrcImageDescriptor imageDesc;
    {
        // Set image color format
        imageDesc.format    = LLGL::ImageFormat::RGBA;

        // Set image data type (unsigned char = 8-bit unsigned integer)
        imageDesc.dataType  = LLGL::DataType::UInt8;

        // Set image buffer source for texture initial data
        imageDesc.data      = imageBuffer;

        // Set image buffer size
        imageDesc.dataSize  = static_cast<std::size_t>(width*height*4);
    }

    // Create texture and upload image data onto hardware texture
    auto tex = renderSys.CreateTexture(
        LLGL::Texture2DDesc(LLGL::Format::RGBA8UNorm, width, height, bindFlags), &imageDesc
    );

    // Release image data
    stbi_image_free(imageBuffer);

    // Show info
    std::cout << "loaded texture: " << filename << std::endl;

    return tex;
}

bool SaveTextureWithRenderer(LLGL::RenderSystem& renderSys, LLGL::Texture& texture, const std::string& filename, std::uint32_t mipLevel)
{
    // Get texture dimension
    auto texSize = texture.GetMipExtent(0);

    // Read texture image data
    std::vector<LLGL::ColorRGBAub> imageBuffer(texSize.width*texSize.height);
    renderSys.ReadTexture(
        texture, mipLevel,
        LLGL::DstImageDescriptor
        {
            LLGL::ImageFormat::RGBA,
            LLGL::DataType::UInt8,
            imageBuffer.data(),
            imageBuffer.size() * sizeof(LLGL::ColorRGBAub)
        }
    );

    // Save image data to file (using STBI library, see https://github.com/nothings/stb)
    auto result = stbi_write_png(
        filename.c_str(),
        static_cast<int>(texSize.width),
        static_cast<int>(texSize.height),
        4,
        imageBuffer.data(),
        static_cast<int>(texSize.width)*4
    );

    if (!result)
    {
        std::cerr << "failed to write texture to file: \"" + filename + "\"" << std::endl;
        return false;
    }

    // Show info
    std::cout << "saved texture: " << filename << std::endl;

    return true;
}

LLGL::Texture* ExampleBase::LoadTexture(const std::string& filename, long bindFlags)
{
    return LoadTextureWithRenderer(*renderer, filename, bindFlags);
}

bool ExampleBase::SaveTexture(LLGL::Texture& texture, const std::string& filename, std::uint32_t mipLevel)
{
    return SaveTextureWithRenderer(*renderer, texture, filename, mipLevel);
}

float ExampleBase::GetAspectRatio() const
{
    auto resolution = context->GetVideoMode().resolution;
    return (static_cast<float>(resolution.width) / static_cast<float>(resolution.height));
}

bool ExampleBase::IsOpenGL() const
{
    return (renderer->GetRendererID() == LLGL::RendererID::OpenGL);
}

bool ExampleBase::IsVulkan() const
{
    return (renderer->GetRendererID() == LLGL::RendererID::Vulkan);
}

bool ExampleBase::IsDirect3D() const
{
    return
    (
        renderer->GetRendererID() == LLGL::RendererID::Direct3D9  ||
        renderer->GetRendererID() == LLGL::RendererID::Direct3D10 ||
        renderer->GetRendererID() == LLGL::RendererID::Direct3D11 ||
        renderer->GetRendererID() == LLGL::RendererID::Direct3D12
    );
}

bool ExampleBase::IsMetal() const
{
    return (renderer->GetRendererID() == LLGL::RendererID::Metal);
}

bool ExampleBase::IsLoadingDone() const
{
    return loadingDone_;
}

Gs::Matrix4f ExampleBase::PerspectiveProjection(float aspectRatio, float near, float far, float fov)
{
    int flags = (IsOpenGL() || IsVulkan() ? Gs::ProjectionFlags::UnitCube : 0);
    return Gs::ProjectionMatrix4f::Perspective(aspectRatio, near, far, fov, flags).ToMatrix4();
}

Gs::Matrix4f ExampleBase::OrthogonalProjection(float width, float height, float near, float far)
{
    int flags = (IsOpenGL() ? Gs::ProjectionFlags::UnitCube : 0);
    return Gs::ProjectionMatrix4f::Orthogonal(width, height, near, far, flags).ToMatrix4();
}

bool ExampleBase::Supported(const LLGL::ShadingLanguage shadingLanguage) const
{
    const auto& languages = renderer->GetRenderingCaps().shadingLanguages;
    return (std::find(languages.begin(), languages.end(), shadingLanguage) != languages.end());
}

