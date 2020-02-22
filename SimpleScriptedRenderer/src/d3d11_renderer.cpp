#include "d3d11_renderer.hpp"
#include "my_assert.hpp"

void d3d11_renderer::init(HWND hwnd)
{
  my_assert(hwnd);
  this->hwnd = hwnd;

  i32 width, height;
  {
    RECT rect;
    GetClientRect(hwnd, &rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
  }

  {
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1 };

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferCount = swapchain_buffer_count;
    scDesc.BufferDesc.Format = swapchain_format;
    scDesc.BufferDesc.Width = (UINT)width;
    scDesc.BufferDesc.Height = (UINT)height;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.OutputWindow = hwnd;
    scDesc.SampleDesc.Count = 1;
    scDesc.SampleDesc.Quality = 0;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    scDesc.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 1,
      D3D11_SDK_VERSION, &scDesc, swapchain.ReleaseAndGetAddressOf(),
      device.ReleaseAndGetAddressOf(), nullptr, nullptr);
    my_assert(SUCCEEDED(hr));

    device->GetImmediateContext(ctx.ReleaseAndGetAddressOf());
  }

  swapchain_width = width;
  swapchain_height = height;
  create_swapchain();
}

void d3d11_renderer::shutdown()
{
  dsv.Reset();
  depth_stencil_buffer.Reset();
  swapchain_rtv.Reset();
  swapchain.Reset();
  ctx.Reset();
  device.Reset();
}

void d3d11_renderer::create_swapchain()
{
  HRESULT hr;

  com_ptr<ID3D11Texture2D> backbuffer;
  {
    hr = swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D),
      (void**)(backbuffer.GetAddressOf()));
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    hr = device->CreateRenderTargetView(backbuffer.Get(), &desc,
                                        swapchain_rtv.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = swapchain_width;
    desc.Height = swapchain_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    hr = device->CreateTexture2D(&desc, nullptr,
                                 depth_stencil_buffer.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_DEPTH_STENCIL_VIEW_DESC desc = {};
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    hr = device->CreateDepthStencilView(depth_stencil_buffer.Get(), &desc,
                                        dsv.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }
}

void d3d11_renderer::resize_swapchain(i32 width, i32 height)
{
  HRESULT hr;
  swapchain_rtv.Reset();
  hr = swapchain->ResizeBuffers(swapchain_buffer_count, (UINT)width, (UINT)height,
                                swapchain_format, 0);
  my_assert(SUCCEEDED(hr));

  swapchain_width = width;
  swapchain_height = height;
  create_swapchain();
}
