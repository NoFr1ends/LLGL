/*
 * D3D12Shader.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2019 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "D3D12Shader.h"
#include "../D3D12Types.h"
#include "../../DXCommon/DXCore.h"
#include "../../DXCommon/DXTypes.h"
#include "../../../Core/Helper.h"
#include <algorithm>
#include <stdexcept>
#include <d3dcompiler.h>


namespace LLGL
{


D3D12Shader::D3D12Shader(const ShaderDescriptor& desc) :
    Shader { desc.type }
{
    if (!BuildShader(desc))
        hasErrors_ = true;
    BuildInputLayout(static_cast<UINT>(desc.vertex.inputAttribs.size()), desc.vertex.inputAttribs.data());
}

bool D3D12Shader::HasErrors() const
{
    return hasErrors_;
}

std::string D3D12Shader::GetReport() const
{
    return (errors_.Get() != nullptr ? DXGetBlobString(errors_.Get()) : "");
}

D3D12_SHADER_BYTECODE D3D12Shader::GetByteCode() const
{
    D3D12_SHADER_BYTECODE byteCode = {};

    if (byteCode_)
    {
        byteCode.pShaderBytecode    = byteCode_->GetBufferPointer();
        byteCode.BytecodeLength     = byteCode_->GetBufferSize();
    }

    return byteCode;
}

bool D3D12Shader::Reflect(ShaderReflection& reflection) const
{
    if (byteCode_)
        return SUCCEEDED(ReflectShaderByteCode(reflection));
    else
        return false;
}

bool D3D12Shader::ReflectNumThreads(Extent3D& numThreads) const
{
    if (byteCode_)
        return SUCCEEDED(ReflectShaderByteCodeNumThreads(numThreads));
    else
        return false;
}

D3D12_INPUT_LAYOUT_DESC D3D12Shader::GetInputLayoutDesc() const
{
    D3D12_INPUT_LAYOUT_DESC desc;
    {
        desc.pInputElementDescs = inputElements_.data();
        desc.NumElements        = static_cast<UINT>(inputElements_.size());
    }
    return desc;
}


/*
 * ======= Private: =======
 */

bool D3D12Shader::BuildShader(const ShaderDescriptor& shaderDesc)
{
    if (IsShaderSourceCode(shaderDesc.sourceType))
        return CompileSource(shaderDesc);
    else
        return LoadBinary(shaderDesc);
}

static DXGI_FORMAT GetInputElementFormat(const VertexAttribute& attrib)
{
    try
    {
        return D3D12Types::Map(attrib.format);
    }
    catch (const std::exception& e)
    {
        throw std::invalid_argument(std::string(e.what()) + " for vertex attribute: " + attrib.name);
    }
}

/*
Converts a vertex attributes to a D3D12 input element descriptor
and stores the semantic name in the specified linear string container
*/
static void Convert(D3D12_INPUT_ELEMENT_DESC& dst, const VertexAttribute& src, LinearStringContainer& stringContainer)
{
    dst.SemanticName            = stringContainer.CopyString(src.name);
    dst.SemanticIndex           = src.semanticIndex;
    dst.Format                  = GetInputElementFormat(src);
    dst.InputSlot               = src.slot;
    dst.AlignedByteOffset       = src.offset;
    dst.InputSlotClass          = (src.instanceDivisor > 0 ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA);
    dst.InstanceDataStepRate    = src.instanceDivisor;
}

void D3D12Shader::BuildInputLayout(UINT numVertexAttribs, const VertexAttribute* vertexAttribs)
{
    if (numVertexAttribs == 0 || vertexAttribs == nullptr)
        return;

    /* Reserve memory for the input element names */
    inputElementNames_.Clear();
    for (UINT i = 0; i < numVertexAttribs; ++i)
        inputElementNames_.Reserve(vertexAttribs[i].name.size());

    /* Build input element descriptors */
    inputElements_.resize(numVertexAttribs);
    for (UINT i = 0; i < numVertexAttribs; ++i)
        Convert(inputElements_[i], vertexAttribs[i], inputElementNames_);
}

// see https://msdn.microsoft.com/en-us/library/windows/desktop/dd607324(v=vs.85).aspx
bool D3D12Shader::CompileSource(const ShaderDescriptor& shaderDesc)
{
    /* Get source code */
    std::string fileContent;
    const char* sourceCode      = nullptr;
    SIZE_T      sourceLength    = 0;

    if (shaderDesc.sourceType == ShaderSourceType::CodeFile)
    {
        fileContent     = ReadFileString(shaderDesc.source);
        sourceCode      = fileContent.c_str();
        sourceLength    = fileContent.size();
    }
    else
    {
        sourceCode      = shaderDesc.source;
        sourceLength    = shaderDesc.sourceSize;
    }

    /* Get parameter from union */
    const char* entry   = shaderDesc.entryPoint;
    const char* target  = (shaderDesc.profile != nullptr ? shaderDesc.profile : "");
    auto        defines = reinterpret_cast<const D3D_SHADER_MACRO*>(shaderDesc.defines);
    auto        flags   = shaderDesc.flags;

    /* Compile shader code */
    auto hr = D3DCompile(
        sourceCode,
        sourceLength,
        nullptr,                            // LPCSTR               pSourceName
        defines,                            // D3D_SHADER_MACRO*    pDefines
        nullptr,                            // ID3DInclude*         pInclude
        entry,                              // LPCSTR               pEntrypoint
        target,                             // LPCSTR               pTarget
        DXGetCompilerFlags(flags),          // UINT                 Flags1
        0,                                  // UINT                 Flags2 (recommended to always be 0)
        byteCode_.ReleaseAndGetAddressOf(), // ID3DBlob**           ppCode
        errors_.ReleaseAndGetAddressOf()    // ID3DBlob**           ppErrorMsgs
    );

    /* Return true if compilation was successful */
    return SUCCEEDED(hr);
}

bool D3D12Shader::LoadBinary(const ShaderDescriptor& shaderDesc)
{
    if (shaderDesc.sourceType == ShaderSourceType::BinaryFile)
    {
        /* Load binary code from file */
        byteCode_ = DXCreateBlob(ReadFileBuffer(shaderDesc.source));
    }
    else
    {
        /* Copy binary code into container and create native shader */
        byteCode_ = DXCreateBlob(shaderDesc.source, shaderDesc.sourceSize);
    }
    return (byteCode_.Get() != nullptr && byteCode_->GetBufferSize() > 0);
}

/*
NOTE:
Most of this code for shader reflection is 1:1 copied from the D3D11 renderer.
However, all descriptors have the "D3D12" prefix, so a generalization (without macros) is tricky.
*/

static ShaderResource* FetchOrInsertResource(
    ShaderReflection&   reflection,
    const char*         name,
    const ResourceType  type,
    std::uint32_t       slot)
{
    /* Fetch resource from list */
    for (auto& resource : reflection.resources)
    {
        if (resource.binding.type == type &&
            resource.binding.slot == slot &&
            resource.binding.name.compare(name) == 0)
        {
            return (&resource);
        }
    }

    /* Allocate new resource and initialize parameters */
    reflection.resources.resize(reflection.resources.size() + 1);
    auto ref = &(reflection.resources.back());
    {
        ref->binding.name = std::string(name);
        ref->binding.type = type;
        ref->binding.slot = slot;
    }
    return ref;
}

// Converts a D3D12 signature parameter into a vertex attribute
static void Convert(VertexAttribute& dst, const D3D12_SIGNATURE_PARAMETER_DESC& src)
{
    dst.name            = std::string(src.SemanticName);
    dst.format          = DXGetSignatureParameterType(src.ComponentType, src.Mask);
    dst.semanticIndex   = src.SemanticIndex;
    dst.systemValue     = DXTypes::Unmap(src.SystemValueType);
}

static HRESULT ReflectShaderVertexAttributes(
    ID3D12ShaderReflection*     reflectionObject,
    const D3D12_SHADER_DESC&    shaderDesc,
    ShaderReflection&           reflection)
{
    for (UINT i = 0; i < shaderDesc.InputParameters; ++i)
    {
        /* Get signature parameter descriptor */
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
        auto hr = reflectionObject->GetInputParameterDesc(i, &paramDesc);
        if (FAILED(hr))
            return hr;

        /* Add vertex input attribute to output list */
        VertexAttribute vertexAttrib;
        Convert(vertexAttrib, paramDesc);
        reflection.vertex.inputAttribs.push_back(vertexAttrib);
    }

    for (UINT i = 0; i < shaderDesc.OutputParameters; ++i)
    {
        /* Get signature parameter descriptor */
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
        auto hr = reflectionObject->GetOutputParameterDesc(i, &paramDesc);
        if (FAILED(hr))
            return hr;

        /* Add vertex output attribute to output list */
        VertexAttribute vertexAttrib;
        Convert(vertexAttrib, paramDesc);
        reflection.vertex.outputAttribs.push_back(vertexAttrib);
    }

    return S_OK;
}

// Converts a D3D12 signature parameter into a fragment attribute
static void Convert(FragmentAttribute& dst, const D3D12_SIGNATURE_PARAMETER_DESC& src)
{
    dst.name        = std::string(src.SemanticName);
    dst.format      = DXGetSignatureParameterType(src.ComponentType, src.Mask);
    dst.location    = src.SemanticIndex;
    dst.systemValue = DXTypes::Unmap(src.SystemValueType);
}

static HRESULT ReflectShaderFragmentAttributes(
    ID3D12ShaderReflection*     reflectionObject,
    const D3D12_SHADER_DESC&    shaderDesc,
    ShaderReflection&           reflection)
{
    for (UINT i = 0; i < shaderDesc.OutputParameters; ++i)
    {
        /* Get signature parameter descriptor */
        D3D12_SIGNATURE_PARAMETER_DESC paramDesc;
        auto hr = reflectionObject->GetOutputParameterDesc(i, &paramDesc);
        if (FAILED(hr))
            return hr;

        /* Add fragment attribute to output list */
        FragmentAttribute fragmentAttrib;
        Convert(fragmentAttrib, paramDesc);
        reflection.fragment.outputAttribs.push_back(fragmentAttrib);
    }

    return S_OK;
}

static void ReflectShaderResourceGeneric(
    const D3D12_SHADER_INPUT_BIND_DESC& inputBindDesc,
    ShaderReflection&                   reflection,
    const ResourceType                  resourceType,
    long                                bindFlags,
    long                                stageFlags,
    const StorageBufferType             storageBufferType   = StorageBufferType::Undefined)
{
    /* Initialize resource view descriptor for a generic resource (texture, sampler, storage buffer etc.) */
    auto resource = FetchOrInsertResource(reflection, inputBindDesc.Name, resourceType, inputBindDesc.BindPoint);
    {
        resource->binding.bindFlags     |= bindFlags;
        resource->binding.stageFlags    |= stageFlags;
        resource->binding.arraySize     = inputBindDesc.BindCount;
        resource->storageBufferType     = storageBufferType;
    }
}

static HRESULT ReflectShaderConstantBuffer(
    ID3D12ShaderReflection*             reflectionObject,
    ShaderReflection&                   reflection,
    const D3D12_SHADER_DESC&            shaderDesc,
    const D3D12_SHADER_INPUT_BIND_DESC& inputBindDesc,
    long                                stageFlags,
    UINT&                               cbufferIdx)
{
    /* Initialize resource view descriptor for constant buffer */
    auto resource = FetchOrInsertResource(reflection, inputBindDesc.Name, ResourceType::Buffer, inputBindDesc.BindPoint);
    {
        resource->binding.bindFlags     |= BindFlags::ConstantBuffer;
        resource->binding.stageFlags    |= stageFlags;
        resource->binding.arraySize     = inputBindDesc.BindCount;
    }

    /* Determine constant buffer size */
    if (cbufferIdx < shaderDesc.ConstantBuffers)
    {
        auto cbufferReflection = reflectionObject->GetConstantBufferByIndex(cbufferIdx++);

        D3D12_SHADER_BUFFER_DESC shaderBufferDesc;
        auto hr = cbufferReflection->GetDesc(&shaderBufferDesc);
        if (FAILED(hr))
            return hr;

        if (shaderBufferDesc.Type == D3D_CT_CBUFFER)
        {
            /* Store constant buffer size in output descriptor */
            resource->constantBufferSize = shaderBufferDesc.Size;
        }
        else
        {
            /* Type mismatch in descriptors */
            return E_FAIL;
        }
    }
    else
    {
        /* Resource index mismatch in descriptor */
        return E_FAIL;
    }

    return S_OK;
}

static HRESULT ReflectShaderInputBindings(
    ID3D12ShaderReflection*     reflectionObject,
    const D3D12_SHADER_DESC&    shaderDesc,
    long                        stageFlags,
    ShaderReflection&           reflection)
{
    HRESULT hr = S_OK;
    UINT cbufferIdx = 0;

    for (UINT i = 0; i < shaderDesc.BoundResources; ++i)
    {
        /* Get shader input resource descriptor */
        D3D12_SHADER_INPUT_BIND_DESC inputBindDesc;
        auto hr = reflectionObject->GetResourceBindingDesc(i, &inputBindDesc);
        if (FAILED(hr))
            return hr;

        /* Reflect shader resource view */
        switch (inputBindDesc.Type)
        {
            case D3D_SIT_CBUFFER:
                hr = ReflectShaderConstantBuffer(reflectionObject, reflection, shaderDesc, inputBindDesc, stageFlags, cbufferIdx);
                break;

            case D3D_SIT_TBUFFER:
            case D3D_SIT_TEXTURE:
                ReflectShaderResourceGeneric(inputBindDesc, reflection, ResourceType::Texture, BindFlags::Sampled, stageFlags);
                break;

            case D3D_SIT_SAMPLER:
                ReflectShaderResourceGeneric(inputBindDesc, reflection, ResourceType::Sampler, 0, stageFlags);
                break;

            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
                ReflectShaderResourceGeneric(inputBindDesc, reflection, DXTypes::Unmap(inputBindDesc.Dimension), BindFlags::Sampled, stageFlags);
                break;

            case D3D_SIT_UAV_RWTYPED:
            case D3D_SIT_UAV_RWSTRUCTURED:
            case D3D_SIT_UAV_RWBYTEADDRESS:
            case D3D_SIT_UAV_APPEND_STRUCTURED:
            case D3D_SIT_UAV_CONSUME_STRUCTURED:
            case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
                ReflectShaderResourceGeneric(inputBindDesc, reflection, DXTypes::Unmap(inputBindDesc.Dimension), BindFlags::Storage, stageFlags);
                break;

            default:
                break;
        }

        if (FAILED(hr))
            return hr;
    }

    return S_OK;
}

HRESULT D3D12Shader::ReflectShaderByteCode(ShaderReflection& reflection) const
{
    HRESULT hr = S_OK;

    /* Get shader reflection */
    ComPtr<ID3D12ShaderReflection> reflectionObject;
    hr = D3DReflect(byteCode_->GetBufferPointer(), byteCode_->GetBufferSize(), IID_PPV_ARGS(reflectionObject.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return hr;

    D3D12_SHADER_DESC shaderDesc;
    hr = reflectionObject->GetDesc(&shaderDesc);
    if (FAILED(hr))
        return hr;

    if (GetType() == ShaderType::Vertex)
    {
        /* Get input parameter descriptors */
        hr = ReflectShaderVertexAttributes(reflectionObject.Get(), shaderDesc, reflection);
        if (FAILED(hr))
            return hr;
    }
    else if (GetType() == ShaderType::Fragment)
    {
        /* Get output parameter descriptors */
        hr = ReflectShaderFragmentAttributes(reflectionObject.Get(), shaderDesc, reflection);
        if (FAILED(hr))
            return hr;
    }

    /* Get input bindings */
    hr = ReflectShaderInputBindings(reflectionObject.Get(), shaderDesc, GetStageFlags(), reflection);
    if (FAILED(hr))
        return hr;

    return S_OK;
}

HRESULT D3D12Shader::ReflectShaderByteCodeNumThreads(Extent3D& numThreads) const
{
    /* Get shader reflection */
    ComPtr<ID3D12ShaderReflection> reflectionObject;
    auto hr = D3DReflect(byteCode_->GetBufferPointer(), byteCode_->GetBufferSize(), IID_PPV_ARGS(reflectionObject.ReleaseAndGetAddressOf()));

    if (SUCCEEDED(hr))
    {
        reflectionObject->GetThreadGroupSize(
            &(numThreads.width),
            &(numThreads.height),
            &(numThreads.depth)
        );
    }

    return hr;
}


} // /namespace LLGL



// ================================================================================
