module;

#include <d3d12.h>
#include <Windows.h>
#include <wrl.h>
#include <queue>
#include <unordered_map>

export module command_queue;

import common;

using Microsoft::WRL::ComPtr;

export class CommandQueue
{
   public:
    ComPtr<ID3D12CommandQueue> queue;

    CommandQueue() = default;
    CommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
    // Adopt an existing ID3D12CommandQueue (e.g. one owned by gfx::IDevice). The
    // queue itself is shared; this CommandQueue still owns its own fence and
    // allocator/list pools.
    CommandQueue(
        ComPtr<ID3D12Device2> device,
        ComPtr<ID3D12CommandQueue> existingQueue,
        D3D12_COMMAND_LIST_TYPE type
    );

    ComPtr<ID3D12GraphicsCommandList2> getCmdList();
    uint64_t execCmdList(ComPtr<ID3D12GraphicsCommandList2> cmdList);
    uint64_t signal();
    bool isFenceComplete(uint64_t fval);
    uint64_t completedFenceValue() const;
    void waitForFenceVal(uint64_t fval);
    void flush();

   private:
    ComPtr<ID3D12Device2> device;

    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    HANDLE fenceEvent;

    struct CmdAllocEntry
    {
        uint64_t fenceValue;
        ComPtr<ID3D12CommandAllocator> commandAllocator;
    };

    D3D12_COMMAND_LIST_TYPE type;
    std::queue<CmdAllocEntry> cmdAllocQueue;
    std::queue<ComPtr<ID3D12GraphicsCommandList2>> cmdListQueue;
    std::unordered_map<ID3D12GraphicsCommandList2*, ComPtr<ID3D12CommandAllocator>>
        cmdListAllocatorMap;

    ComPtr<ID3D12CommandAllocator> createCmdAlloc();
    ComPtr<ID3D12GraphicsCommandList2> createCmdList(ComPtr<ID3D12CommandAllocator> allocator);
};
