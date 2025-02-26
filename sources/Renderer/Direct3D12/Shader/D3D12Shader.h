/*
 * D3D12Shader.h
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2019 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#ifndef LLGL_D3D12_SHADER_H
#define LLGL_D3D12_SHADER_H


#include <LLGL/Shader.h>
#include <LLGL/ShaderProgramFlags.h>
#include <LLGL/VertexAttribute.h>
#include <LLGL/BufferFlags.h>
#include "../../DXCommon/ComPtr.h"
#include "../../../Core/LinearStringContainer.h"
#include <vector>
#include <d3d12.h>


namespace LLGL
{


class D3D12Shader final : public Shader
{

    public:

        D3D12Shader(const ShaderDescriptor& desc);

        bool HasErrors() const override;

        std::string GetReport() const override;

    public:

        D3D12_SHADER_BYTECODE GetByteCode() const;

        bool Reflect(ShaderReflection& reflection) const;
        bool ReflectNumThreads(Extent3D& numThreads) const;

        D3D12_INPUT_LAYOUT_DESC GetInputLayoutDesc() const;

    private:

        bool BuildShader(const ShaderDescriptor& shaderDesc);
        void BuildInputLayout(UINT numVertexAttribs, const VertexAttribute* vertexAttribs);

        bool CompileSource(const ShaderDescriptor& shaderDesc);
        bool LoadBinary(const ShaderDescriptor& shaderDesc);

        HRESULT ReflectShaderByteCode(ShaderReflection& reflection) const;
        HRESULT ReflectShaderByteCodeNumThreads(Extent3D& numThreads) const;

    private:

        ComPtr<ID3DBlob>                        byteCode_;

        ComPtr<ID3DBlob>                        errors_;
        bool                                    hasErrors_  = false;

        std::vector<D3D12_INPUT_ELEMENT_DESC>   inputElements_;
        LinearStringContainer                   inputElementNames_; // custom string container to hold valid string pointers.

};


} // /namespace LLGL


#endif



// ================================================================================
