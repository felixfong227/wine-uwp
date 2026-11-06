/* Compile: x86_64-w64-mingw32-gcc probe-corewindow-swapchain.c -o probe.exe -ldxgi -ld3d12 -ldxguid
 * Run beside the candidate DXGI/D3D12 DLLs with native overrides and
 * DXVK_CONFIG="dxgi.enableDummyCompositionSwapchain = True".
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <stdio.h>

static const GUID core_iid = {0x45d64a29,0xa63e,0x4cb6,{0xb4,0x98,0x57,0x81,0xd2,0x98,0xcb,0x4f}};
struct core;
struct core_vtbl {
    HRESULT (WINAPI *query)(struct core *, REFIID, void **);
    ULONG (WINAPI *addref)(struct core *);
    ULONG (WINAPI *release)(struct core *);
    HRESULT (WINAPI *get_hwnd)(struct core *, HWND *);
    HRESULT (WINAPI *put_capture)(struct core *, BOOL);
};
struct core { const struct core_vtbl *vtbl; HWND hwnd; ULONG refs; };
static HRESULT WINAPI query(struct core *c, REFIID iid, void **out) {
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &core_iid) && !IsEqualGUID(iid, &IID_IUnknown)) return E_NOINTERFACE;
    *out = c; ++c->refs; return S_OK;
}
static ULONG WINAPI addref(struct core *c) { return ++c->refs; }
static ULONG WINAPI release(struct core *c) { return --c->refs; }
static HRESULT WINAPI get_hwnd(struct core *c, HWND *out) { if (!out) return E_POINTER; *out=c->hwnd; return S_OK; }
static HRESULT WINAPI put_capture(struct core *c, BOOL capture) { return E_NOTIMPL; }
static const struct core_vtbl core_vtbl = { query, addref, release, get_hwnd, put_capture };
#define CHECK(call) do { hr=(call); if(FAILED(hr)) { printf("COREWINDOW_FAIL %s hr=%08lx\n", #call, (unsigned long)hr); goto done; } } while(0)
int main(int argc, char **argv) {
    IDXGIFactory2 *factory=NULL;
    ID3D12Device *device=NULL;
    ID3D12CommandQueue *queue=NULL;
    IDXGISwapChain1 *swap=NULL;
    D3D12_COMMAND_QUEUE_DESC qdesc={0};
    DXGI_SWAP_CHAIN_DESC1 desc={0};
    struct core core={&core_vtbl,NULL,1};
    HRESULT hr=E_FAIL;
    int result=1;
    core.hwnd=CreateWindowExA(0,"STATIC","CoreWindow swap-chain API probe",WS_OVERLAPPEDWINDOW,0,0,320,200,NULL,NULL,NULL,NULL);
    if (!core.hwnd) return 2;
    CHECK(CreateDXGIFactory1(&IID_IDXGIFactory2,(void **)&factory));
    CHECK(D3D12CreateDevice(NULL,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void **)&device));
    CHECK(ID3D12Device_CreateCommandQueue(device,&qdesc,&IID_ID3D12CommandQueue,(void **)&queue));
    desc.Width=320; desc.Height=200; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
    if(argc > 1)
        CHECK(IDXGIFactory2_CreateSwapChainForHwnd(factory,(IUnknown *)queue,core.hwnd,&desc,NULL,NULL,&swap));
    else
        CHECK(IDXGIFactory2_CreateSwapChainForCoreWindow(factory,(IUnknown *)queue,(IUnknown *)&core,&desc,NULL,&swap));
    if (!swap || core.refs!=1) { puts("COREWINDOW_FAIL null swap or reference leak"); goto done; }
    {
        HWND actual=NULL;
        CHECK(IDXGISwapChain1_GetHwnd(swap,&actual));
        if(actual != core.hwnd) { puts("COREWINDOW_FAIL wrong HWND"); goto done; }
    }
    IDXGISwapChain1_Release(swap); swap=NULL;
    if (core.refs != 1) { puts("COREWINDOW_FAIL reference leak after destruction"); goto done; }
    if(argc > 1) { puts("HWND_PASS created D3D12 swapchain with correct window"); result=0; goto done; }
    hr=IDXGIFactory2_CreateSwapChainForCoreWindow(factory,(IUnknown *)queue,NULL,&desc,NULL,&swap);
    if(hr != DXGI_ERROR_INVALID_CALL || swap) { puts("COREWINDOW_FAIL null window validation"); goto done; }
    hr=IDXGIFactory2_CreateSwapChainForCoreWindow(factory,(IUnknown *)queue,(IUnknown *)&core,&desc,NULL,NULL);
    if(hr != DXGI_ERROR_INVALID_CALL) { puts("COREWINDOW_FAIL null output validation"); goto done; }
    hr=IDXGIFactory2_CreateSwapChainForCoreWindow(factory,NULL,(IUnknown *)&core,&desc,NULL,&swap);
    if(hr != DXGI_ERROR_INVALID_CALL || swap) { puts("COREWINDOW_FAIL null device validation"); goto done; }
    hr=IDXGIFactory2_CreateSwapChainForCoreWindow(factory,(IUnknown *)queue,(IUnknown *)&core,NULL,NULL,&swap);
    if(hr != DXGI_ERROR_INVALID_CALL || swap) { puts("COREWINDOW_FAIL null description validation"); goto done; }
    {
        HWND saved = core.hwnd;
        core.hwnd = NULL;
        hr=IDXGIFactory2_CreateSwapChainForCoreWindow(factory,(IUnknown *)queue,(IUnknown *)&core,&desc,NULL,&swap);
        core.hwnd = saved;
        if(hr != DXGI_ERROR_INVALID_CALL || swap || core.refs != 1) { puts("COREWINDOW_FAIL null HWND or reference leak"); goto done; }
    }
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    CHECK(IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)queue, &desc, NULL, &swap));
    {
        IDXGISwapChain3 *swap3 = NULL;
        UINT masks[2] = {1, 1};
        IUnknown *queues[2] = {(IUnknown *)queue, (IUnknown *)queue};
        CHECK(IDXGISwapChain1_QueryInterface(swap, &IID_IDXGISwapChain3, (void **)&swap3));
        hr = IDXGISwapChain3_ResizeBuffers1(swap3, 2, 640, 400, DXGI_FORMAT_R8G8B8A8_UNORM, desc.Flags, masks, queues);
        IDXGISwapChain3_Release(swap3);
        if (FAILED(hr)) { printf("COREWINDOW_FAIL dummy ResizeBuffers1 hr=%08lx\n", (unsigned long)hr); goto done; }
    }
    IDXGISwapChain1_Release(swap); swap = NULL;
    puts("COREWINDOW_PASS CoreWindow HWND, balanced references, null validation, dummy composition ResizeBuffers1");
    result=0;
done:
    if(swap) IDXGISwapChain1_Release(swap);
    if(queue) ID3D12CommandQueue_Release(queue);
    if(device) ID3D12Device_Release(device);
    if(factory) IDXGIFactory2_Release(factory);
    DestroyWindow(core.hwnd);
    return result;
}
