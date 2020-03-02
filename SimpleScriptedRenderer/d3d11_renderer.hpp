#pragma once
#include <d3d11.h>
#include <wrl.h>
#include "types.hpp"

template <class T>
using com_ptr = Microsoft::WRL::ComPtr<T>;

struct d3d11_renderer
{
  HWND m_hwnd;
  com_ptr<IDXGIFactory> factory;
  com_ptr<ID3D11Device> device;
  DXGI_SWAP_CHAIN_DESC swapchain_desc;
  com_ptr<IDXGISwapChain> swapchain;
  com_ptr<ID3D11DeviceContext> ctx;
  com_ptr<ID3D11RenderTargetView> swapchain_rtv;
  com_ptr<ID3D11Texture2D> depth_stencil_buffer;
  com_ptr<ID3D11DepthStencilView> dsv;

  void init(HWND hwnd);
  void shutdown();
  void create_swapchain();
  void resize_swapchain(i32 width, i32 height);
  void destroy_swapchain();
  void set_multisample_count(u32 sample_count);
};