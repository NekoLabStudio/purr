#ifdef _WIN32

#include "dxgi_adapter.hpp"

#include <dxgi1_6.h>
// The interface IDs, which MinGW's headers leave out (as Godot's Direct3D 12 driver includes it).
#include <dxguids/dxguids.h>

// The high-performance GPU where DXGI can say which that is (IDXGIFactory6), otherwise the first.
bool dxgi_high_performance_adapter(LUID& luid) {
    IDXGIAdapter1* adapter = nullptr;
    IDXGIFactory6* factory6 = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory6)))) {
        if (FAILED(factory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {
            adapter = nullptr;
        }
        factory6->Release();
    }
    if (!adapter) {
        IDXGIFactory1* factory = nullptr;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }
        const HRESULT found = factory->EnumAdapters1(0, &adapter);
        factory->Release();
        if (FAILED(found)) {
            return false;
        }
    }
    DXGI_ADAPTER_DESC1 description{};
    adapter->GetDesc1(&description);
    adapter->Release();
    luid = description.AdapterLuid;
    return true;
}

std::string dxgi_adapter_name(const LUID& luid) {
    std::string name;
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return name;
    }
    IDXGIAdapter1* adapter = nullptr;
    if (SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)))) {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        char utf8[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, utf8, sizeof(utf8), nullptr, nullptr);
        name = utf8;
        adapter->Release();
    }
    factory->Release();
    return name;
}

#endif  // _WIN32
