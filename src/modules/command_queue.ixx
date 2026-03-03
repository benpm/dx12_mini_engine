module;

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <queue>

export module command_queue;

export import common;

export class CommandQueue
{
   public:
    ComPtr<ID3D12CommandQueue> queue;

    CommandQueue() = default;
    CommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);

    ComPtr<ID3D12GraphicsCommandList2> getCmdList();
    uint64_t execCmdList(ComPtr<ID3D12GraphicsCommandList2> cmdList);
    uint64_t signal();
    bool isFenceComplete(uint64_t fval);
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

    ComPtr<ID3D12CommandAllocator> createCmdAlloc();
    ComPtr<ID3D12GraphicsCommandList2> createCmdList(ComPtr<ID3D12CommandAllocator> allocator);
};
