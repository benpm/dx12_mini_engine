module;

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <cassert>
#include <cstdint>

module command_queue;

CommandQueue::CommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
    : device(device), fenceValue(0), type(type)
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.NodeMask = 0;

    chkDX(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&this->queue)));
    chkDX(device->CreateFence(this->fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&this->fence)));
    this->fenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);

    assert(this->fenceEvent && "Failed to create fence event handle.");
}

// Get or create a commandlist, using next available command allocator
ComPtr<ID3D12GraphicsCommandList2> CommandQueue::getCmdList()
{
    ComPtr<ID3D12GraphicsCommandList2> commandList;
    ComPtr<ID3D12CommandAllocator> commandAllocator = this->createCmdAlloc();

    if (this->cmdListQueue.empty()) {
        commandList = this->createCmdList(commandAllocator);
    } else {
        commandList = this->cmdListQueue.front();
        chkDX(commandList->Reset(commandAllocator.Get(), nullptr));
        this->cmdListQueue.pop();
    }

    // Assign allocator to command list
    chkDX(commandList->SetPrivateDataInterface(
        __uuidof(ID3D12CommandAllocator), commandAllocator.Get()
    ));

    return commandList;
}

uint64_t CommandQueue::execCmdList(ComPtr<ID3D12GraphicsCommandList2> cmdList)
{
    chkDX(cmdList->Close());

    // Retrieve command allocator from private data of command list
    ID3D12CommandAllocator* commandAllocator;
    UINT dataSize = sizeof(commandAllocator);
    chkDX(cmdList->GetPrivateData(__uuidof(ID3D12CommandAllocator), &dataSize, &commandAllocator));

    ID3D12CommandList* const commandLists[] = { cmdList.Get() };
    this->queue->ExecuteCommandLists(_countof(commandLists), commandLists);
    uint64_t fenceVal = this->signal();

    // Push bach the allocator and list to their queues so they can be re-used
    this->cmdAllocQueue.emplace(CmdAllocEntry{ fenceVal, commandAllocator });
    this->cmdListQueue.push(cmdList);

    // Release ownership of the command allocator
    commandAllocator->Release();

    return fenceVal;
}

uint64_t CommandQueue::signal()
{
    uint64_t fenceValueForSignal = ++fenceValue;
    chkDX(this->queue->Signal(fence.Get(), fenceValueForSignal));

    return fenceValueForSignal;
}

bool CommandQueue::isFenceComplete(uint64_t fval)
{
    return (this->fence->GetCompletedValue() >= fval);
}

void CommandQueue::waitForFenceVal(uint64_t fval)
{
    if (!this->isFenceComplete(fval)) {
        chkDX(fence->SetEventOnCompletion(fval, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);
    }
}

void CommandQueue::flush()
{
    uint64_t fenceValueForSignal = this->signal();
    this->waitForFenceVal(fenceValueForSignal);
}

// Command allocator can't be re-used unless associated cmd list's commands
//  have finished on the GPU (i.e. entry's fence value is greater than the
//  current fence value)
ComPtr<ID3D12CommandAllocator> CommandQueue::createCmdAlloc()
{
    ComPtr<ID3D12CommandAllocator> allocator;

    if (this->cmdAllocQueue.size() > 0 &&
        this->isFenceComplete(this->cmdAllocQueue.front().fenceValue)) {
        allocator = this->cmdAllocQueue.front().commandAllocator;
        chkDX(allocator->Reset());
        this->cmdAllocQueue.pop();
    } else {
        chkDX(device->CreateCommandAllocator(this->type, IID_PPV_ARGS(&allocator)));
    }

    return allocator;
}

ComPtr<ID3D12GraphicsCommandList2> CommandQueue::createCmdList(
    ComPtr<ID3D12CommandAllocator> allocator
)
{
    ComPtr<ID3D12GraphicsCommandList2> commandList;
    chkDX(device->CreateCommandList(
        0, this->type, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList)
    ));

    return commandList;
}
