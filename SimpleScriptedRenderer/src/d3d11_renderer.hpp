#pragma once
#include <d3d11.h>
#include <wrl.h>
#include "types.hpp"

template <class T>
using com_ptr = Microsoft::WRL::ComPtr<T>;

struct d3d11_renderer
{
  HWND hwnd;
  com_ptr<ID3D11Device> device;
  DXGI_FORMAT swapchain_format = DXGI_FORMAT_R8G8B8A8_UNORM;
  u32 swapchain_width;
  u32 swapchain_height;
  u32 swapchain_buffer_count = 2;
  com_ptr<IDXGISwapChain> swapchain;
  com_ptr<ID3D11DeviceContext> ctx;
  com_ptr<ID3D11RenderTargetView> swapchain_rtv;
  com_ptr<ID3D11Texture2D> depth_stencil_buffer;
  com_ptr<ID3D11DepthStencilView> dsv;

  void init(HWND hwnd);
  void shutdown();
  void create_swapchain();
  void resize_swapchain(i32 width, i32 height);
};