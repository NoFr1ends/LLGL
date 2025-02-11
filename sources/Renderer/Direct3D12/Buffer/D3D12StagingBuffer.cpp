/*
 * D3D12StagingBuffer.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015-2019 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "D3D12StagingBuffer.h"
#include "../../DXCommon/DXCore.h"
#include "../D3DX12/d3dx12.h"
#include <string.h>


namespace LLGL
{


D3D12StagingBuffer::D3D12StagingBuffer(ID3D12Device* device, UINT64 size)
{
    Create(device, size);
}

D3D12StagingBuffer::D3D12StagingBuffer(D3D12StagingBuffer&& rhs) :
    native_ { std::move(rhs.native_) },
    size_   { rhs.size_              },
    offset_ { rhs.offset_            }
{
}

D3D12StagingBuffer& D3D12StagingBuffer::operator = (D3D12StagingBuffer&& rhs)
{
    if (this != &rhs)
    {
        native_ = std::move(rhs.native_);
        size_   = rhs.size_;
        offset_ = rhs.offset_;
    }
    return *this;
}

void D3D12StagingBuffer::Create(ID3D12Device* device, UINT64 size)
{
    /* Create GPU upload buffer */
    auto hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(native_.ReleaseAndGetAddressOf())
    );
    DXThrowIfCreateFailed(hr, "failed to create D3D12 committed resource for upload buffer");

    /* Store new size and reset write offset */
    size_   = size;
    offset_ = 0;
}

void D3D12StagingBuffer::Release()
{
    native_.Reset();
}

void D3D12StagingBuffer::Reset()
{
    offset_ = 0;
}

bool D3D12StagingBuffer::Capacity(UINT64 dataSize) const
{
    return (offset_ + dataSize <= size_);
}

void D3D12StagingBuffer::Write(
    ID3D12GraphicsCommandList*  commandList,
    ID3D12Resource*             dstBuffer,
    UINT64                      dstOffset,
    const void*                 data,
    UINT64                      dataSize)
{
    /* Copy CPU-memory to upload buffer */
    char* intermediateData = nullptr;
    HRESULT hr = native_->Map(0, nullptr, reinterpret_cast<void**>(&intermediateData));
    if (SUCCEEDED(hr))
    {
        ::memcpy(intermediateData + offset_, data, static_cast<std::size_t>(dataSize));
        native_->Unmap(0, nullptr);
    }

    /* Encode copy buffer command */
    commandList->CopyBufferRegion(dstBuffer, dstOffset, native_.Get(), offset_, dataSize);
}

void D3D12StagingBuffer::WriteAndIncrementOffset(
    ID3D12GraphicsCommandList*  commandList,
    ID3D12Resource*             dstBuffer,
    UINT64                      dstOffset,
    const void*                 data,
    UINT64                      dataSize)
{
    Write(commandList, dstBuffer, dstOffset, data, dataSize);
    offset_ += dataSize;
}


} // /namespace LLGL



// ================================================================================
