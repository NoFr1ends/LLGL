/*
 * D3D12ShaderProgram.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2019 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "D3D12ShaderProgram.h"
#include "D3D12Shader.h"
#include "../../CheckedCast.h"
#include "../../../Core/Helper.h"
#include <LLGL/Log.h>
#include <LLGL/VertexAttribute.h>
#include <algorithm>
#include <stdexcept>


namespace LLGL
{


static void Attach(D3D12Shader*& shaderD3D, Shader* shader)
{
    if (shader != nullptr)
        shaderD3D = LLGL_CAST(D3D12Shader*, shader);
}

D3D12ShaderProgram::D3D12ShaderProgram(const ShaderProgramDescriptor& desc)
{
    Attach(vs_, desc.vertexShader);
    Attach(hs_, desc.tessControlShader);
    Attach(ds_, desc.tessEvaluationShader);
    Attach(gs_, desc.geometryShader);
    Attach(ps_, desc.fragmentShader);
    Attach(cs_, desc.computeShader);
    LinkProgram();
}

bool D3D12ShaderProgram::HasErrors() const
{
    return (linkError_ != LinkError::NoError);
}

std::string D3D12ShaderProgram::GetReport() const
{
    if (auto s = ShaderProgram::LinkErrorToString(linkError_))
        return s;
    else
        return "";
}

bool D3D12ShaderProgram::Reflect(ShaderReflection& reflection) const
{
    ShaderProgram::ClearShaderReflection(reflection);

    for (auto shader : shaders_)
    {
        if (shader != nullptr)
        {
            if (!shader->Reflect(reflection))
                return false;
        }
    }
    ShaderProgram::FinalizeShaderReflection(reflection);
    return true;
}

UniformLocation D3D12ShaderProgram::FindUniformLocation(const char* name) const
{
    return -1; // TODO
}

bool D3D12ShaderProgram::SetWorkGroupSize(const Extent3D& workGroupSize)
{
    return false; // dummy
}

bool D3D12ShaderProgram::GetWorkGroupSize(Extent3D& workGroupSize) const
{
    if (cs_ != nullptr)
        return cs_->ReflectNumThreads(workGroupSize);
    else
        return false;
}

D3D12_INPUT_LAYOUT_DESC D3D12ShaderProgram::GetInputLayoutDesc() const
{
    if (vs_ != nullptr)
        return vs_->GetInputLayoutDesc();
    else
        return {};
}


/*
 * ======= Private: =======
 */

void D3D12ShaderProgram::LinkProgram()
{
    /* Validate shader composition */
    linkError_ = LinkError::NoError;

    /* Validate native shader objects */
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (shaders_[i] != nullptr && shaders_[i]->GetByteCode().BytecodeLength == 0)
            linkError_ = LinkError::InvalidByteCode;
    }

    /*
    Validate composition of attached shaders
    Note: reinterpret_cast allowed here, because no multiple inheritance is used, just plain pointers!
    */
    if (!ShaderProgram::ValidateShaderComposition(reinterpret_cast<Shader* const*>(shaders_), 6))
        linkError_ = LinkError::InvalidComposition;
}


} // /namespace LLGL



// ================================================================================
