#include "d3d11_renderer.hpp"
#include "my_assert.hpp"

void d3d11_renderer::init(HWND hwnd)
{
  my_assert(hwnd);
  m_hwnd = hwnd;

  i32 width, height;
  {
    RECT rect;
    GetClientRect(hwnd, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
  }

  HRESULT hr;

  {
    hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)factory.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_1 };
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, feature_levels, 1,
                           D3D11_SDK_VERSION, device.ReleaseAndGetAddressOf(), nullptr, ctx.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  swapchain_desc = {};
  swapchain_desc.BufferCount = 2;
  swapchain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapchain_desc.BufferDesc.Width = (UINT)width;
  swapchain_desc.BufferDesc.Height = (UINT)height;
  swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapchain_desc.OutputWindow = hwnd;
  swapchain_desc.SampleDesc.Count = 1;
  swapchain_desc.SampleDesc.Quality = 0;
  swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  swapchain_desc.Windowed = TRUE;

  create_swapchain();
}

void d3d11_renderer::shutdown()
{
  destroy_swapchain();
  ctx.Reset();
  device.Reset();
}

void d3d11_renderer::create_swapchain()
{
  HRESULT hr;

  if (swapchain.Get() == nullptr)
  {
    hr = factory->CreateSwapChain(device.Get(), &swapchain_desc, swapchain.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  com_ptr<ID3D11Texture2D> backbuffer;
  {
    hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D),
      (void**)(backbuffer.GetAddressOf()));
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.ViewDimension = swapchain_desc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = device->CreateRenderTargetView(backbuffer.Get(), &desc,
                                        swapchain_rtv.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = swapchain_desc.BufferDesc.Width;
    desc.Height = swapchain_desc.BufferDesc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = swapchain_desc.SampleDesc.Count;
    desc.SampleDesc.Quality = swapchain_desc.SampleDesc.Quality;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = device->CreateTexture2D(&desc, nullptr,
                                 depth_stencil_buffer.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.ViewDimension = swapchain_desc.SampleDesc.Count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    hr = device->CreateDepthStencilView(depth_stencil_buffer.Get(), &desc,
                                        dsv.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }
}

void d3d11_renderer::resize_swapchain(i32 width, i32 height)
{
  if (width == 0 || height == 0)
    return;

  swapchain_desc.BufferDesc.Width = (u32)width;
  swapchain_desc.BufferDesc.Height = (u32)height;

  HRESULT hr;
  swapchain_rtv.Reset();
  hr = swapchain->ResizeBuffers(swapchain_desc.BufferCount, swapchain_desc.BufferDesc.Width, swapchain_desc.BufferDesc.Height,
                                swapchain_desc.BufferDesc.Format, swapchain_desc.Flags);
  my_assert(SUCCEEDED(hr));

  create_swapchain();
}

void d3d11_renderer::destroy_swapchain()
{
  dsv.Reset();
  depth_stencil_buffer.Reset();
  swapchain_rtv.Reset();
  swapchain.Reset();
}

void d3d11_renderer::set_multisample_count(u32 sample_count)
{
  if (sample_count == 1 || sample_count == 2 || sample_count == 4 || sample_count == 8)
  {
    swapchain_desc.SampleDesc.Count = sample_count;
    destroy_swapchain();
    create_swapchain();
  }
}
