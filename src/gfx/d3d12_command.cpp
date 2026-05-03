// Queue + CommandList impl for the gfx D3D12 backend.

#include <algorithm>

#include "d3d12_internal.h"

import common;

using Microsoft::WRL::ComPtr;

namespace gfxd3d12
{

    // ============================================================================
    // BindlessHeap
    // ============================================================================

    void BindlessHeap::init(
        ID3D12Device2* device,
        uint32_t capacity,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        bool shaderVisible,
        const wchar_t* name
    )
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.NumDescriptors = capacity;
        desc.Type = type;
        desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                   : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        chkDX(
            device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)),
            "BindlessHeap::CreateDescriptorHeap"
        );
        if (name) {
            heap->SetName(name);
        }
        descSize = device->GetDescriptorHandleIncrementSize(type);
        capacity_ = capacity;
        next.store(1);  // 0 reserved as null
        cpuStart = heap->GetCPUDescriptorHandleForHeapStart();
        if (shaderVisible) {
            gpuStart = heap->GetGPUDescriptorHandleForHeapStart();
        }
    }

    uint32_t BindlessHeap::allocate()
    {
        std::lock_guard<std::mutex> lk(mu);
        if (!freeList.empty()) {
            uint32_t i = freeList.back();
            freeList.pop_back();
            return i;
        }
        uint32_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= capacity_) {
            throw std::runtime_error("BindlessHeap: out of descriptors");
        }
        return i;
    }

    uint32_t BindlessHeap::allocateBatch(uint32_t count)
    {
        if (count == 0) {
            return UINT32_MAX;
        }
        uint32_t base = next.fetch_add(count, std::memory_order_relaxed);
        if (base + count > capacity_) {
            throw std::runtime_error("BindlessHeap: out of descriptors (batch)");
        }
        return base;
    }

    void BindlessHeap::free(uint32_t index)
    {
        if (index == 0 || index == UINT32_MAX) {
            return;
        }
        std::lock_guard<std::mutex> lk(mu);
        freeList.push_back(index);
    }

    void BindlessHeap::freeBatch(uint32_t base, uint32_t count)
    {
        if (base == UINT32_MAX || count == 0) {
            return;
        }
        std::lock_guard<std::mutex> lk(mu);
        for (uint32_t i = 0; i < count; ++i) {
            freeList.push_back(base + i);
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE BindlessHeap::cpuHandle(uint32_t index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE h = cpuStart;
        h.ptr += static_cast<size_t>(index) * descSize;
        return h;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE BindlessHeap::gpuHandle(uint32_t index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE h = gpuStart;
        h.ptr += static_cast<uint64_t>(index) * descSize;
        return h;
    }

    // ============================================================================
    // Queue
    // ============================================================================

    Queue::Queue(Device* dev, ID3D12Device2* d3dDev, D3D12_COMMAND_LIST_TYPE type)
        : device(dev), d3dDevice(d3dDev), type(type)
    {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = type;
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        qd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        chkDX(
            d3dDevice->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "Queue::CreateCommandQueue"
        );
        chkDX(
            d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
            "Queue::CreateFence"
        );
        fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent) {
            throwLastWin32Error("Queue::CreateEventW");
        }
    }

    Queue::~Queue()
    {
        flush();
        if (fenceEvent) {
            ::CloseHandle(fenceEvent);
        }
    }

    ComPtr<ID3D12CommandAllocator> Queue::getAllocator()
    {
        if (!allocPool.empty() && fence->GetCompletedValue() >= allocPool.front().fenceValue) {
            auto a = allocPool.front().alloc;
            allocPool.pop_front();
            chkDX(a->Reset(), "CommandAllocator::Reset");
            return a;
        }
        ComPtr<ID3D12CommandAllocator> a;
        chkDX(d3dDevice->CreateCommandAllocator(type, IID_PPV_ARGS(&a)), "CreateCommandAllocator");
        return a;
    }

    gfx::ICommandList* Queue::acquireCommandList()
    {
        auto alloc = getAllocator();
        ComPtr<ID3D12GraphicsCommandList2> list;
        if (!listPool.empty()) {
            list = listPool.front();
            listPool.pop_front();
            chkDX(list->Reset(alloc.Get(), nullptr), "CommandList::Reset");
        } else {
            ComPtr<ID3D12GraphicsCommandList> base;
            chkDX(
                d3dDevice->CreateCommandList(0, type, alloc.Get(), nullptr, IID_PPV_ARGS(&base)),
                "CreateCommandList"
            );
            chkDX(base.As(&list), "CommandList QI ID3D12GraphicsCommandList2");
        }
        listAllocs[list.Get()] = alloc;
        liveLists.push_back(list);

        auto wrapper = std::make_unique<CommandList>(device, list.Get());
        auto* raw = wrapper.get();
        wrappers.push_back(std::move(wrapper));
        return raw;
    }

    gfx::FenceValue Queue::submit(gfx::ICommandList* list)
    {
        auto* impl = static_cast<CommandList*>(list);
        ID3D12CommandList* raw = impl->native();
        queue->ExecuteCommandLists(1, &raw);
        auto fv = signal();

        auto* nativeList = impl->native();
        auto it = listAllocs.find(nativeList);
        if (it != listAllocs.end()) {
            allocPool.push_back({ fv.value, it->second });
            listAllocs.erase(it);
        }
        listPool.emplace_back(nativeList);

        auto rm = std::remove_if(
            wrappers.begin(), wrappers.end(),
            [&](const std::unique_ptr<CommandList>& cl) { return cl.get() == impl; }
        );
        wrappers.erase(rm, wrappers.end());
        return fv;
    }

    gfx::FenceValue Queue::signal()
    {
        ++fenceValue;
        chkDX(queue->Signal(fence.Get(), fenceValue), "Queue::Signal");
        return { fenceValue };
    }

    bool Queue::isFenceComplete(gfx::FenceValue fv) const
    {
        return fence->GetCompletedValue() >= fv.value;
    }

    gfx::FenceValue Queue::completedFenceValue() const
    {
        return { fence->GetCompletedValue() };
    }

    void Queue::waitForFence(gfx::FenceValue fv)
    {
        if (fence->GetCompletedValue() < fv.value) {
            chkDX(fence->SetEventOnCompletion(fv.value, fenceEvent), "Fence::SetEventOnCompletion");
            ::WaitForSingleObject(fenceEvent, INFINITE);
        }
    }

    void Queue::flush()
    {
        waitForFence(signal());
    }

    // ============================================================================
    // CommandList
    // ============================================================================

    void CommandList::begin()
    {
        ID3D12DescriptorHeap* heaps[] = {
            device->srvHeap().native(),
            device->samplerHeapRef().native(),
        };
        cmd->SetDescriptorHeaps(2, heaps);
        cmd->SetGraphicsRootSignature(device->bindlessRootSig());
        cmd->SetComputeRootSignature(device->bindlessRootSig());
        cmd->SetGraphicsRootDescriptorTable(3, device->srvHeap().gpuHandle(0));
        cmd->SetGraphicsRootDescriptorTable(4, device->samplerHeapRef().gpuHandle(0));
        cmd->SetComputeRootDescriptorTable(3, device->srvHeap().gpuHandle(0));
        cmd->SetComputeRootDescriptorTable(4, device->samplerHeapRef().gpuHandle(0));
    }

    void CommandList::end()
    {
        chkDX(cmd->Close(), "CommandList::Close");
    }

    void CommandList::beginRenderPass(const gfx::RenderPassDesc& d)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8];
        uint32_t rtvCount = 0;
        for (uint32_t i = 0; i < d.numColorAttachments; ++i) {
            const auto& ca = d.colorAttachments[i];
            if (!ca.texture.isValid()) {
                continue;
            }
            rtvHandles[rtvCount].ptr =
                static_cast<SIZE_T>(device->rtvHandle(ca.texture, ca.arraySlice));
            ++rtvCount;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsvH{};
        bool hasDsv = d.hasDepth && d.depthAttachment.texture.isValid();
        if (hasDsv) {
            dsvH.ptr = static_cast<SIZE_T>(
                device->dsvHandle(d.depthAttachment.texture, d.depthAttachment.arraySlice)
            );
        }

        cmd->OMSetRenderTargets(
            rtvCount, rtvCount > 0 ? rtvHandles : nullptr, FALSE, hasDsv ? &dsvH : nullptr
        );

        for (uint32_t i = 0; i < rtvCount; ++i) {
            if (d.colorAttachments[i].loadOp == gfx::LoadOp::Clear) {
                cmd->ClearRenderTargetView(
                    rtvHandles[i], d.colorAttachments[i].clearColor, 0, nullptr
                );
            }
        }
        if (hasDsv) {
            const auto& da = d.depthAttachment;
            D3D12_CLEAR_FLAGS clearFlags = {};
            if (da.depthLoadOp == gfx::LoadOp::Clear) {
                clearFlags |= D3D12_CLEAR_FLAG_DEPTH;
            }
            if (da.stencilLoadOp == gfx::LoadOp::Clear) {
                clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
            }
            if (clearFlags) {
                cmd->ClearDepthStencilView(
                    dsvH, clearFlags, da.clearDepth, da.clearStencil, 0, nullptr
                );
            }
        }
    }

    void CommandList::setRenderTargets(
        std::span<const gfx::TextureHandle> colors,
        gfx::TextureHandle depth,
        uint32_t arraySlice
    )
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8];
        uint32_t rtvCount = 0;
        for (auto h : colors) {
            if (!h.isValid() || rtvCount >= 8) {
                continue;
            }
            rtvHandles[rtvCount].ptr = static_cast<SIZE_T>(device->rtvHandle(h, arraySlice));
            ++rtvCount;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dsvH{};
        bool hasDsv = depth.isValid();
        if (hasDsv) {
            dsvH.ptr = static_cast<SIZE_T>(device->dsvHandle(depth, arraySlice));
        }
        cmd->OMSetRenderTargets(
            rtvCount, rtvCount > 0 ? rtvHandles : nullptr, FALSE, hasDsv ? &dsvH : nullptr
        );
    }

    void CommandList::clearRenderTarget(gfx::TextureHandle texture, const float color[4])
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv;
        rtv.ptr = static_cast<SIZE_T>(device->rtvHandle(texture, 0));
        cmd->ClearRenderTargetView(rtv, color, 0, nullptr);
    }

    void CommandList::clearDepthStencil(
        gfx::TextureHandle texture,
        gfx::ClearFlags flags,
        float depth,
        uint8_t stencil
    )
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv;
        dsv.ptr = static_cast<SIZE_T>(device->dsvHandle(texture, 0));
        D3D12_CLEAR_FLAGS f = {};
        if (gfx::any(flags, gfx::ClearFlags::Depth)) {
            f |= D3D12_CLEAR_FLAG_DEPTH;
        }
        if (gfx::any(flags, gfx::ClearFlags::Stencil)) {
            f |= D3D12_CLEAR_FLAG_STENCIL;
        }
        cmd->ClearDepthStencilView(dsv, f, depth, stencil, 0, nullptr);
    }

    void CommandList::setStencilRef(uint32_t ref) { cmd->OMSetStencilRef(ref); }

    void CommandList::setViewport(const gfx::Viewport& vp)
    {
        D3D12_VIEWPORT v{ vp.x, vp.y, vp.width, vp.height, vp.minDepth, vp.maxDepth };
        cmd->RSSetViewports(1, &v);
    }

    void CommandList::setScissor(const gfx::ScissorRect& sr)
    {
        D3D12_RECT r{ sr.x, sr.y, sr.x + sr.width, sr.y + sr.height };
        cmd->RSSetScissorRects(1, &r);
    }

    void CommandList::bindPipeline(gfx::PipelineHandle h)
    {
        auto* p = device->getPipeline(h);
        if (!p) {
            return;
        }
        // Set the matching root signature first so D3D12 validation sees a
        // PSO/RS pair that agrees. Setting the same RS that's already bound
        // is a no-op (D3D12 spec preserves bindings); switching to a new RS
        // unbinds prior bindings, but that's the correct behavior — the
        // caller will re-bind for the new layout.
        if (p->rootSig) {
            if (p->isCompute) {
                cmd->SetComputeRootSignature(p->rootSig.Get());
            } else {
                cmd->SetGraphicsRootSignature(p->rootSig.Get());
            }
        }
        cmd->SetPipelineState(p->pso.Get());
        if (!p->isCompute) {
            cmd->IASetPrimitiveTopology(p->topology);
        }
    }

    void CommandList::setRootConstants(uint32_t slot, const void* data, uint32_t numDwords)
    {
        cmd->SetGraphicsRoot32BitConstants(slot, numDwords, data, 0);
    }

    void CommandList::setComputeRootConstants(uint32_t slot, const void* data, uint32_t numDwords)
    {
        cmd->SetComputeRoot32BitConstants(slot, numDwords, data, 0);
    }

    void CommandList::setConstantBuffer(uint32_t slot, gfx::BufferHandle b, uint64_t offset)
    {
        auto* rec = device->getBuffer(b);
        if (!rec) {
            return;
        }
        cmd->SetGraphicsRootConstantBufferView(
            slot, rec->resource->GetGPUVirtualAddress() + offset
        );
    }

    void CommandList::setComputeConstantBuffer(uint32_t slot, gfx::BufferHandle b, uint64_t offset)
    {
        auto* rec = device->getBuffer(b);
        if (!rec) {
            return;
        }
        cmd->SetComputeRootConstantBufferView(
            slot, rec->resource->GetGPUVirtualAddress() + offset
        );
    }

    void CommandList::setVertexBuffer(
        uint32_t slot,
        gfx::BufferHandle b,
        uint32_t stride,
        uint64_t offset
    )
    {
        auto* rec = device->getBuffer(b);
        if (!rec) {
            return;
        }
        D3D12_VERTEX_BUFFER_VIEW v{};
        v.BufferLocation = rec->resource->GetGPUVirtualAddress() + offset;
        v.SizeInBytes = static_cast<UINT>(rec->desc.size - offset);
        v.StrideInBytes = stride;
        cmd->IASetVertexBuffers(slot, 1, &v);
    }

    void CommandList::setIndexBuffer(gfx::BufferHandle b, gfx::IndexFormat fmt, uint64_t offset)
    {
        auto* rec = device->getBuffer(b);
        if (!rec) {
            return;
        }
        D3D12_INDEX_BUFFER_VIEW v{};
        v.BufferLocation = rec->resource->GetGPUVirtualAddress() + offset;
        v.SizeInBytes = static_cast<UINT>(rec->desc.size - offset);
        v.Format = (fmt == gfx::IndexFormat::Uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        cmd->IASetIndexBuffer(&v);
    }

    void CommandList::draw(uint32_t v, uint32_t i, uint32_t fv, uint32_t fi)
    {
        cmd->DrawInstanced(v, i, fv, fi);
    }

    void
    CommandList::drawIndexed(uint32_t ic, uint32_t inst, uint32_t fi, int32_t bv, uint32_t fInst)
    {
        cmd->DrawIndexedInstanced(ic, inst, fi, bv, fInst);
    }

    void CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z)
    {
        cmd->Dispatch(x, y, z);
    }

    void
    CommandList::barrier(gfx::TextureHandle h, gfx::ResourceState before, gfx::ResourceState after)
    {
        auto* rec = device->getTexture(h);
        if (!rec) {
            return;
        }
        auto b = CD3DX12_RESOURCE_BARRIER::Transition(
            rec->resource.Get(), toD3D12States(before), toD3D12States(after)
        );
        cmd->ResourceBarrier(1, &b);
        rec->currentState = toD3D12States(after);
    }

    void
    CommandList::barrier(gfx::BufferHandle h, gfx::ResourceState before, gfx::ResourceState after)
    {
        auto* rec = device->getBuffer(h);
        if (!rec) {
            return;
        }
        auto b = CD3DX12_RESOURCE_BARRIER::Transition(
            rec->resource.Get(), toD3D12States(before), toD3D12States(after)
        );
        cmd->ResourceBarrier(1, &b);
        rec->currentState = toD3D12States(after);
    }

    void CommandList::uavBarrier(gfx::TextureHandle h)
    {
        auto* rec = device->getTexture(h);
        if (!rec) {
            return;
        }
        auto b = CD3DX12_RESOURCE_BARRIER::UAV(rec->resource.Get());
        cmd->ResourceBarrier(1, &b);
    }

    void CommandList::barriers(std::span<const gfx::TextureBarrier> ts)
    {
        if (ts.empty()) {
            return;
        }
        // Up to 16 in one batch — render-graph passes touch far fewer.
        D3D12_RESOURCE_BARRIER batch[16];
        uint32_t count = 0;
        for (const auto& t : ts) {
            if (count >= 16) {
                cmd->ResourceBarrier(count, batch);
                count = 0;
            }
            auto* rec = device->getTexture(t.handle);
            if (!rec) {
                continue;
            }
            auto before = toD3D12States(t.before);
            auto after = toD3D12States(t.after);
            if (before == after) {
                continue;
            }
            batch[count++] = CD3DX12_RESOURCE_BARRIER::Transition(rec->resource.Get(), before, after);
            rec->currentState = after;
        }
        if (count > 0) {
            cmd->ResourceBarrier(count, batch);
        }
    }

    void CommandList::copyBuffer(
        gfx::BufferHandle dst,
        uint64_t dOff,
        gfx::BufferHandle src,
        uint64_t sOff,
        uint64_t size
    )
    {
        auto* dr = device->getBuffer(dst);
        auto* sr = device->getBuffer(src);
        if (!dr || !sr) {
            return;
        }
        cmd->CopyBufferRegion(dr->resource.Get(), dOff, sr->resource.Get(), sOff, size);
    }

    void CommandList::copyTextureToBuffer(
        gfx::BufferHandle dst,
        uint64_t dOff,
        gfx::TextureHandle src,
        uint32_t srcMip,
        uint32_t srcSlice,
        int32_t x,
        int32_t y,
        uint32_t w,
        uint32_t h
    )
    {
        auto* dr = device->getBuffer(dst);
        auto* sr = device->getTexture(src);
        if (!dr || !sr) {
            return;
        }
        D3D12_TEXTURE_COPY_LOCATION dstLoc{};
        dstLoc.pResource = dr->resource.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 totalBytes = 0;
        UINT numRows = 0;
        UINT64 rowSize = 0;
        auto td = sr->resource->GetDesc();
        device->d3d()->GetCopyableFootprints(
            &td, srcMip, 1, dOff, &fp, &numRows, &rowSize, &totalBytes
        );
        dstLoc.PlacedFootprint = fp;

        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = sr->resource.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        srcLoc.SubresourceIndex = D3D12CalcSubresource(
            srcMip, srcSlice, 0, sr->desc.mipLevels, sr->desc.depthOrArraySize
        );

        D3D12_BOX box{ static_cast<UINT>(x),     static_cast<UINT>(y),     0,
                       static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1 };
        cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &box);
    }

}  // namespace gfxd3d12

namespace gfx
{
    std::unique_ptr<ICommandList> wrapNativeCommandList(IDevice* dev, void* nativeList)
    {
        if (!dev || !nativeList) {
            return nullptr;
        }
        if (dev->backend() != BackendKind::D3D12) {
            throw std::runtime_error("wrapNativeCommandList: backend mismatch");
        }
        auto* d3dDev = static_cast<gfxd3d12::Device*>(dev);
        auto* d3dList = static_cast<ID3D12GraphicsCommandList2*>(nativeList);
        return std::make_unique<gfxd3d12::CommandList>(d3dDev, d3dList);
    }
}  // namespace gfx
