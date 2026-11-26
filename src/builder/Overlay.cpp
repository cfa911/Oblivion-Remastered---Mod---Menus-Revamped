// In-game Pack Builder overlay, Phase 1 (KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md).
// SPDX-License-Identifier: GPL-3.0-or-later
// Rendering: MinHook on dxgi.dll's CreateDXGIFactory/1/2; the factory's CreateSwapChain (slot 10) and
// CreateSwapChainForHwnd (IDXGIFactory2 slot 15) capture the D3D12 command queue; the swap chain's Present (8),
// ResizeBuffers (13), Present1 (22) and ResizeBuffers1 (IDXGISwapChain3 slot 39) draw a Dear ImGui frame before the
// game's present and follow resizes. Our GPU objects belong to one swap chain: a new chain (fullscreen/HDR switch), a new
// buffer count or format rebuilds them and restarts ImGui's DX12 backend; our back-buffer references are dropped before
// the game resizes or makes a new chain (DXGI refuses either while they are held). Nothing is created before the window
// first opens. Input: Menus Revamped's window procedure and XInputGetState chain call HandleWindowMessage / FilterPad;
// ingame::KeyGate and ingame::PadGate (InputGate.h) decide what the game must not see. Keyboard and mouse only (owner,
// 2026-11-21): F11 or the gear button (Gear.cpp) opens it; F11, Escape (no text field active) or Close closes it.
// Open/close requests from any thread are posted to the game window and applied on its thread.
#include "builder/Builder.h"
#include "builder/InputGate.h"
#include "builder/OverlayRules.h"
#include "builder/LivePreview.h"
#include "builder/Shell.h"
#include "core/Config.h"

#include "engine/Menus.h"
#include "engine/Ui.h"

#include <d3d12.h>
#include <dxgi1_4.h>

#include <MinHook.h>
#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace builder
{
	namespace
	{
		constexpr const char* kMarker = "MR-INGAME-BUILDER-P1";
		constexpr UINT kToggleKey = VK_F11;   // bound nowhere in the game (KB game-internals.md "Keyboard F-key bindings")
		constexpr UINT kSrvCount = 64;
		constexpr UINT kOpenMessage = WM_APP + 0x4D1;   // wParam 1 open, 0 close; posted to the game window

		using CreateFactory_t = HRESULT(WINAPI*)(REFIID, void**);
		using CreateFactory2_t = HRESULT(WINAPI*)(UINT, REFIID, void**);
		using CreateSwapChain_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
		using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
		using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
		using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
		using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
		using ResizeBuffers1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);

		CreateFactory_t          g_createFactory = nullptr;
		CreateFactory_t          g_createFactory1 = nullptr;
		CreateFactory2_t         g_createFactory2 = nullptr;
		CreateSwapChain_t        g_createSwapChain = nullptr;
		CreateSwapChainForHwnd_t g_createSwapChainForHwnd = nullptr;
		Present_t                g_present = nullptr;
		Present1_t               g_present1 = nullptr;
		ResizeBuffers_t          g_resizeBuffers = nullptr;
		ResizeBuffers1_t         g_resizeBuffers1 = nullptr;

		std::atomic<bool> g_open{ false };
		std::atomic<bool> g_failed{ false };
		std::atomic<ID3D12CommandQueue*> g_queue{ nullptr };
		std::atomic<HWND> g_gameWindow{ nullptr };
		std::once_flag g_factoryOnce, g_factory2Once, g_swapOnce;

		// game window thread only
		ingame::KeyGate g_keys;

		// game thread only (XInput chain and core::Tick)
		ingame::PadGate    g_pad;
		ingame::CloseWatch g_watch;

		// window messages for ImGui, queued on the window thread and fed on the render thread before NewFrame
		struct Message { UINT msg; WPARAM w; LPARAM l; };
		std::mutex g_queueLock;
		std::vector<Message> g_messages;

		// --- D3D12 state (render thread, under g_renderLock) ---
		struct Frame
		{
			ID3D12CommandAllocator*     allocator = nullptr;
			ID3D12Resource*             buffer = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
			UINT64                      fenceValue = 0;
		};
		std::recursive_mutex        g_renderLock;
		bool                        g_context = false;     // ImGui context and font
		bool                        g_win32 = false;       // ImGui Win32 backend started (for g_window)
		bool                        g_dx12 = false;        // ImGui DX12 backend started (for the current device objects)
		IDXGISwapChain*             g_chain = nullptr;     // the chain our device objects belong to (not referenced)
		HWND                        g_window = nullptr;
		ID3D12Device*               g_device = nullptr;
		ID3D12DescriptorHeap*       g_rtvHeap = nullptr;
		ID3D12DescriptorHeap*       g_srvHeap = nullptr;
		ID3D12GraphicsCommandList*  g_list = nullptr;
		ID3D12Fence*                g_fence = nullptr;
		HANDLE                      g_fenceEvent = nullptr;
		UINT64                      g_fenceCounter = 0;
		std::vector<Frame>          g_frames;
		std::vector<bool>           g_srvUsed;
		UINT                        g_srvStep = 0;
		DXGI_FORMAT                 g_format = DXGI_FORMAT_UNKNOWN;
		float                       g_styleHeight = 0;
		UINT                        g_height = 0;

		template <class T>
		void Release(T*& a_object)
		{
			if (a_object) {
				a_object->Release();
				a_object = nullptr;
			}
		}

		// --- descriptor allocator for the ImGui DX12 backend ---
		void AllocSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* a_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* a_gpu)
		{
			for (UINT i = 0; i < g_srvUsed.size(); ++i) {
				if (g_srvUsed[i]) continue;
				g_srvUsed[i] = true;
				a_cpu->ptr = g_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(i) * g_srvStep;
				a_gpu->ptr = g_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(i) * g_srvStep;
				return;
			}
			logger::warn("{}: out of SRV descriptors", kMarker);
			*a_cpu = g_srvHeap->GetCPUDescriptorHandleForHeapStart();
			*a_gpu = g_srvHeap->GetGPUDescriptorHandleForHeapStart();
		}

		void FreeSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE a_cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
		{
			if (!g_srvHeap || !g_srvStep) return;
			const auto index = (a_cpu.ptr - g_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr) / g_srvStep;
			if (index < g_srvUsed.size()) g_srvUsed[index] = false;
		}

		void WaitIdle()
		{
			auto* queue = g_queue.load();
			if (!queue || !g_fence || !g_fenceEvent) return;
			const UINT64 value = ++g_fenceCounter;
			if (FAILED(queue->Signal(g_fence, value))) return;
			if (g_fence->GetCompletedValue() < value && SUCCEEDED(g_fence->SetEventOnCompletion(value, g_fenceEvent)))
				WaitForSingleObject(g_fenceEvent, 1000);
		}

		void ReleaseBuffers()
		{
			for (auto& frame : g_frames) Release(frame.buffer);
		}

		bool AcquireBuffers(IDXGISwapChain* a_chain)
		{
			const UINT step = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			auto handle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			for (UINT i = 0; i < g_frames.size(); ++i) {
				if (FAILED(a_chain->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].buffer)))) return false;
				g_frames[i].rtv = handle;
				g_device->CreateRenderTargetView(g_frames[i].buffer, nullptr, handle);
				handle.ptr += step;
			}
			return true;
		}

		// Everything tied to one swap chain; the ImGui context and Win32 backend survive.
		void DestroyDeviceObjects()
		{
			WaitIdle();
			livepreview::ReleaseAll();   // the queue is idle: the preview's upload buffers are done
			if (g_dx12) {
				ImGui_ImplDX12_Shutdown();
				g_dx12 = false;
			}
			ReleaseBuffers();
			for (auto& frame : g_frames) Release(frame.allocator);
			g_frames.clear();
			Release(g_list);
			Release(g_fence);
			Release(g_rtvHeap);
			Release(g_srvHeap);
			Release(g_device);
			if (g_fenceEvent) {
				CloseHandle(g_fenceEvent);
				g_fenceEvent = nullptr;
			}
			g_srvUsed.clear();
			g_chain = nullptr;
		}

		bool CreateDeviceObjects(IDXGISwapChain* a_chain)
		{
			auto* queue = g_queue.load();
			if (!queue) {
				logger::warn("{}: no D3D12 command queue was captured (the plugin loaded after the swap chain was made?) - overlay off", kMarker);
				return false;
			}
			IDXGISwapChain1* chain1 = nullptr;
			if (FAILED(a_chain->QueryInterface(IID_PPV_ARGS(&chain1)))) return false;
			DXGI_SWAP_CHAIN_DESC1 desc{};
			chain1->GetDesc1(&desc);
			HWND window = nullptr;
			chain1->GetHwnd(&window);
			Release(chain1);
			if (FAILED(a_chain->GetDevice(IID_PPV_ARGS(&g_device)))) return false;

			D3D12_DESCRIPTOR_HEAP_DESC rtv{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, desc.BufferCount, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0 };
			D3D12_DESCRIPTOR_HEAP_DESC srv{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvCount, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0 };
			if (FAILED(g_device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&g_rtvHeap))) ||
				FAILED(g_device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&g_srvHeap))))
				return false;
			g_srvStep = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			g_srvUsed.assign(kSrvCount, false);

			g_frames.resize(desc.BufferCount);
			for (auto& frame : g_frames)
				if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)))) return false;
			if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].allocator, nullptr, IID_PPV_ARGS(&g_list))))
				return false;
			g_list->Close();
			if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)))) return false;
			g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
			if (!g_fenceEvent || !AcquireBuffers(a_chain)) return false;

			if (g_win32 && window != g_window) {
				ImGui_ImplWin32_Shutdown();
				g_win32 = false;
			}
			if (!g_win32) {
				g_window = window;
				if (!ImGui_ImplWin32_Init(g_window)) return false;
				g_win32 = true;
			}
			ImGui_ImplDX12_InitInfo info;
			info.Device = g_device;
			info.CommandQueue = queue;
			info.NumFramesInFlight = static_cast<int>(desc.BufferCount);
			info.RTVFormat = desc.Format;
			info.DSVFormat = DXGI_FORMAT_UNKNOWN;
			info.SrvDescriptorHeap = g_srvHeap;
			info.SrvDescriptorAllocFn = &AllocSrv;
			info.SrvDescriptorFreeFn = &FreeSrv;
			if (!ImGui_ImplDX12_Init(&info)) return false;
			g_dx12 = true;
			g_format = desc.Format;
			g_height = desc.Height;
			g_chain = a_chain;
			logger::warn("{}: overlay bound to the swap chain ({} buffers, format {}, {}x{})", kMarker, desc.BufferCount, static_cast<int>(desc.Format),
				desc.Width, desc.Height);
			return true;
		}

		void Fail(const char* a_why)
		{
			g_failed = true;
			g_open = false;
			logger::warn("{}: {} - the in-game builder stays off this session", kMarker, a_why);
		}

		void Render(IDXGISwapChain* a_chain)
		{
			if (g_failed.load() || !g_open.load()) return;
			std::scoped_lock lock(g_renderLock);
			if (!g_context) {
				IMGUI_CHECKVERSION();
				ImGui::CreateContext();
				auto& io = ImGui::GetIO();
				io.IniFilename = nullptr;
				io.LogFilename = nullptr;
				io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
				shell::SetPluginFolder(mrconfig::PluginFolder());
				livepreview::SetPluginFolder(mrconfig::PluginFolder());
				shell::SetLiveHooks({ [](const std::string& a_text) { livepreview::Colours(a_text); }, [] { return livepreview::Status(); },
					[] { return livepreview::StatusIsError(); } });
				shell::LoadFont();
				g_context = true;
			}
			if (a_chain != g_chain) {
				DestroyDeviceObjects();
				if (!CreateDeviceObjects(a_chain)) {
					DestroyDeviceObjects();
					Fail("the overlay could not bind to the swap chain");
					return;
				}
			}
			if (g_styleHeight != static_cast<float>(g_height)) {
				g_styleHeight = static_cast<float>(g_height);
				shell::ApplyStyle(g_styleHeight);
			}
			IDXGISwapChain3* chain3 = nullptr;
			if (FAILED(a_chain->QueryInterface(IID_PPV_ARGS(&chain3)))) return;
			const UINT index = chain3->GetCurrentBackBufferIndex();
			Release(chain3);
			if (index >= g_frames.size() || !g_frames[index].buffer) return;

			std::vector<Message> messages;
			{
				std::scoped_lock queueLock(g_queueLock);
				messages.swap(g_messages);
			}
			for (const auto& m : messages) ImGui_ImplWin32_WndProcHandler(g_window, m.msg, m.w, m.l);

			auto& io = ImGui::GetIO();

			ClipCursor(nullptr);
			io.MouseDrawCursor = true;
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);   // a colour picker or a question: Escape closes it first
			bool keep = shell::Draw();
			if (keep && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !io.WantTextInput && !popup) keep = false;   // Escape, unless typing
			if (!keep) RequestOpen(false, "Close / Escape");
			ImGui::Render();

			auto& frame = g_frames[index];
			if (g_fence->GetCompletedValue() < frame.fenceValue && SUCCEEDED(g_fence->SetEventOnCompletion(frame.fenceValue, g_fenceEvent)))
				WaitForSingleObject(g_fenceEvent, 100);
			frame.allocator->Reset();
			g_list->Reset(frame.allocator, nullptr);
			livepreview::Record(g_device, g_list, g_fence->GetCompletedValue());   // Phase 4a: the baked previews' copies, before our draw
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = frame.buffer;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			g_list->ResourceBarrier(1, &barrier);
			g_list->OMSetRenderTargets(1, &frame.rtv, FALSE, nullptr);
			g_list->SetDescriptorHeaps(1, &g_srvHeap);
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_list);
			std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
			g_list->ResourceBarrier(1, &barrier);
			g_list->Close();
			ID3D12CommandList* lists[] = { g_list };
			auto* queue = g_queue.load();
			queue->ExecuteCommandLists(1, lists);
			frame.fenceValue = ++g_fenceCounter;
			queue->Signal(g_fence, frame.fenceValue);
			livepreview::Submitted(frame.fenceValue);
		}

		thread_local bool t_inside = false;

		HRESULT STDMETHODCALLTYPE OnPresent(IDXGISwapChain* a_chain, UINT a_sync, UINT a_flags)
		{
			if (!t_inside && !(a_flags & DXGI_PRESENT_TEST)) {
				t_inside = true;
				Render(a_chain);
				t_inside = false;
			}
			return g_present(a_chain, a_sync, a_flags);
		}

		HRESULT STDMETHODCALLTYPE OnPresent1(IDXGISwapChain1* a_chain, UINT a_sync, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
		{
			if (!t_inside && !(a_flags & DXGI_PRESENT_TEST)) {
				t_inside = true;
				Render(a_chain);
				t_inside = false;
			}
			return g_present1(a_chain, a_sync, a_flags, a_params);
		}

		// Before the game resizes: our references to its back buffers must go. After: same count and format = new
		// buffers for the same objects; anything else = everything is rebuilt at the next drawn frame.
		void BeforeResize(IDXGISwapChain* a_chain)
		{
			if (a_chain != g_chain) return;
			WaitIdle();
			ReleaseBuffers();
		}

		void AfterResize(IDXGISwapChain* a_chain, HRESULT a_hr)
		{
			if (a_chain != g_chain) return;
			IDXGISwapChain1* chain1 = nullptr;
			DXGI_SWAP_CHAIN_DESC1 desc{};
			if (SUCCEEDED(a_hr) && SUCCEEDED(a_chain->QueryInterface(IID_PPV_ARGS(&chain1)))) {
				chain1->GetDesc1(&desc);
				Release(chain1);
			}
			const bool same = SUCCEEDED(a_hr) && desc.BufferCount == g_frames.size() && desc.Format == g_format;
			if (same && AcquireBuffers(a_chain)) {
				g_height = desc.Height;
				logger::warn("{}: swap chain resized to {}x{}", kMarker, desc.Width, desc.Height);
				return;
			}
			logger::warn("{}: swap chain changed (result 0x{:X}, {} buffers, format {}) - rebuilt at the next drawn frame", kMarker,
				static_cast<std::uint32_t>(a_hr), desc.BufferCount, static_cast<int>(desc.Format));
			ReleaseBuffers();
			g_chain = nullptr;   // Render sees a different chain and rebuilds
		}

		HRESULT STDMETHODCALLTYPE OnResizeBuffers(IDXGISwapChain* a_chain, UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags)
		{
			std::scoped_lock lock(g_renderLock);
			BeforeResize(a_chain);
			const HRESULT hr = g_resizeBuffers(a_chain, a_count, a_width, a_height, a_format, a_flags);
			AfterResize(a_chain, hr);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE OnResizeBuffers1(IDXGISwapChain3* a_chain, UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags,
			const UINT* a_nodes, IUnknown* const* a_queues)
		{
			std::scoped_lock lock(g_renderLock);
			BeforeResize(a_chain);
			const HRESULT hr = g_resizeBuffers1(a_chain, a_count, a_width, a_height, a_format, a_flags, a_nodes, a_queues);
			AfterResize(a_chain, hr);
			return hr;
		}

		// A new swap chain for the window (fullscreen mode, HDR): drop our references to the old one's buffers first.
		void BeforeNewChain()
		{
			std::scoped_lock lock(g_renderLock);
			if (!g_chain) return;
			WaitIdle();
			ReleaseBuffers();
			g_chain = nullptr;
			logger::warn("{}: the game is making a new swap chain - the overlay rebinds at the next drawn frame", kMarker);
		}

		void HookSwapChain(IUnknown* a_chain)
		{
			std::call_once(g_swapOnce, [a_chain] {
				auto** vtable = *reinterpret_cast<void***>(a_chain);
				bool ok = MH_CreateHook(vtable[8], &OnPresent, reinterpret_cast<void**>(&g_present)) == MH_OK &&
				          MH_CreateHook(vtable[13], &OnResizeBuffers, reinterpret_cast<void**>(&g_resizeBuffers)) == MH_OK &&
				          MH_CreateHook(vtable[22], &OnPresent1, reinterpret_cast<void**>(&g_present1)) == MH_OK;
				IDXGISwapChain3* chain3 = nullptr;
				if (ok && SUCCEEDED(a_chain->QueryInterface(IID_PPV_ARGS(&chain3)))) {
					ok = MH_CreateHook((*reinterpret_cast<void***>(chain3))[39], &OnResizeBuffers1, reinterpret_cast<void**>(&g_resizeBuffers1)) == MH_OK;
					chain3->Release();
				}
				ok = ok && MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
				logger::info("{}: swap chain hooks {}", kMarker, ok ? "installed" : "FAILED");
			});
		}

		void CaptureQueue(IUnknown* a_device)
		{
			ID3D12CommandQueue* queue = nullptr;
			if (a_device && SUCCEEDED(a_device->QueryInterface(IID_PPV_ARGS(&queue)))) {
				auto* old = g_queue.exchange(queue);
				if (old) old->Release();
			}
		}

		HRESULT STDMETHODCALLTYPE OnCreateSwapChain(IDXGIFactory* a_factory, IUnknown* a_device, DXGI_SWAP_CHAIN_DESC* a_desc, IDXGISwapChain** a_out)
		{
			BeforeNewChain();
			CaptureQueue(a_device);
			const HRESULT hr = g_createSwapChain(a_factory, a_device, a_desc, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) HookSwapChain(*a_out);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE OnCreateSwapChainForHwnd(IDXGIFactory2* a_factory, IUnknown* a_device, HWND a_window, const DXGI_SWAP_CHAIN_DESC1* a_desc,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_full, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
		{
			BeforeNewChain();
			CaptureQueue(a_device);
			const HRESULT hr = g_createSwapChainForHwnd(a_factory, a_device, a_window, a_desc, a_full, a_output, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) HookSwapChain(*a_out);
			return hr;
		}

		void HookFactory(void* a_factory)
		{
			auto* unknown = static_cast<IUnknown*>(a_factory);
			std::call_once(g_factoryOnce, [unknown] {
				auto** vtable = *reinterpret_cast<void***>(unknown);
				const bool ok = MH_CreateHook(vtable[10], &OnCreateSwapChain, reinterpret_cast<void**>(&g_createSwapChain)) == MH_OK &&
				                MH_EnableHook(vtable[10]) == MH_OK;
				logger::info("{}: factory CreateSwapChain hook {}", kMarker, ok ? "installed" : "FAILED");
			});
			IDXGIFactory2* factory2 = nullptr;
			if (SUCCEEDED(unknown->QueryInterface(IID_PPV_ARGS(&factory2)))) {
				std::call_once(g_factory2Once, [factory2] {
					auto** vtable = *reinterpret_cast<void***>(factory2);
					const bool ok = MH_CreateHook(vtable[15], &OnCreateSwapChainForHwnd, reinterpret_cast<void**>(&g_createSwapChainForHwnd)) == MH_OK &&
					                MH_EnableHook(vtable[15]) == MH_OK;
					logger::info("{}: factory CreateSwapChainForHwnd hook {}", kMarker, ok ? "installed" : "FAILED");
				});
				factory2->Release();
			}
		}

		HRESULT WINAPI OnCreateFactory(REFIID a_iid, void** a_out)
		{
			const HRESULT hr = g_createFactory(a_iid, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) HookFactory(*a_out);
			return hr;
		}

		HRESULT WINAPI OnCreateFactory1(REFIID a_iid, void** a_out)
		{
			const HRESULT hr = g_createFactory1(a_iid, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) HookFactory(*a_out);
			return hr;
		}

		HRESULT WINAPI OnCreateFactory2(UINT a_flags, REFIID a_iid, void** a_out)
		{
			const HRESULT hr = g_createFactory2(a_flags, a_iid, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) HookFactory(*a_out);
			return hr;
		}

		std::optional<ingame::Input> Classify(UINT a_message, WPARAM a_wparam, unsigned& a_code)
		{
			using ingame::Input;
			a_code = 0;
			switch (a_message) {
			case WM_KEYDOWN: case WM_SYSKEYDOWN: a_code = static_cast<unsigned>(a_wparam); return Input::KeyDown;
			case WM_KEYUP: case WM_SYSKEYUP: a_code = static_cast<unsigned>(a_wparam); return Input::KeyUp;
			case WM_CHAR: case WM_SYSCHAR: case WM_UNICHAR: return Input::Char;
			case WM_MOUSEMOVE: return Input::MouseMove;
			case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: return Input::MouseDown;
			case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: a_code = 1; return Input::MouseDown;
			case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: a_code = 2; return Input::MouseDown;
			case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: a_code = GET_XBUTTON_WPARAM(a_wparam) == XBUTTON1 ? 3 : 4; return Input::MouseDown;
			case WM_LBUTTONUP: return Input::MouseUp;
			case WM_RBUTTONUP: a_code = 1; return Input::MouseUp;
			case WM_MBUTTONUP: a_code = 2; return Input::MouseUp;
			case WM_XBUTTONUP: a_code = GET_XBUTTON_WPARAM(a_wparam) == XBUTTON1 ? 3 : 4; return Input::MouseUp;
			case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: return Input::Wheel;
			case WM_INPUT: return Input::Raw;
			default: return std::nullopt;
			}
		}

		void QueueForImGui(UINT a_message, WPARAM a_wparam, LPARAM a_lparam)
		{
			std::scoped_lock lock(g_queueLock);
			if (g_messages.size() < 512) g_messages.push_back({ a_message, a_wparam, a_lparam });
		}

		// game window thread
		void SetOpenNow(bool a_open)
		{
			if (g_failed.load() || a_open == g_open.load()) return;
			g_keys.SetOpen(a_open);
			if (!a_open) {
				std::scoped_lock lock(g_queueLock);
				g_messages.clear();
				livepreview::WindowClosed();   // a first stock extraction stops; the next edit starts it again
			}
			g_open = a_open;
		}

		ingame::Context CurrentContext()
		{
			ingame::Context context;
			auto* im = RE::InterfaceManager::GetInstance(false, false);
			context.menuMode = im ? im->menuMode : -1;
			context.menu = static_cast<int>(menus::Active());
			context.world = ui::RealTimeSeconds() >= 0.0;
			return context;
		}
	}

	void Install()
	{
		HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
		if (!dxgi || MH_Initialize() != MH_OK) {
			logger::warn("{}: dxgi.dll or MinHook unavailable - no in-game builder", kMarker);
			return;
		}
		struct Target { const char* name; void* detour; void** original; };
		const Target targets[] = {
			{ "CreateDXGIFactory", reinterpret_cast<void*>(&OnCreateFactory), reinterpret_cast<void**>(&g_createFactory) },
			{ "CreateDXGIFactory1", reinterpret_cast<void*>(&OnCreateFactory1), reinterpret_cast<void**>(&g_createFactory1) },
			{ "CreateDXGIFactory2", reinterpret_cast<void*>(&OnCreateFactory2), reinterpret_cast<void**>(&g_createFactory2) },
		};
		int hooked = 0;
		for (const auto& t : targets) {
			void* target = reinterpret_cast<void*>(GetProcAddress(dxgi, t.name));
			if (target && MH_CreateHook(target, t.detour, t.original) == MH_OK && MH_EnableHook(target) == MH_OK) ++hooked;
		}
		if (hooked == 0) logger::warn("{}: no dxgi factory hook - no in-game builder", kMarker);
		else logger::warn("{}: in-game builder installed (F11, player-menu gear), {} of 3 dxgi factory hooks", kMarker, hooked);
	}

	bool IsOpen() { return g_open.load(); }

	void RequestOpen(bool a_open, const char* a_why)
	{
		if (g_failed.load()) return;
		HWND window = g_gameWindow.load();
		if (window && PostMessageW(window, kOpenMessage, a_open ? 1 : 0, 0)) logger::info("{}: {} ({})", kMarker, a_open ? "open" : "close", a_why);
	}

	void Tick()
	{
		static bool s_watching = false;
		if (g_open.load()) {
			const auto context = CurrentContext();
			if (!s_watching) {
				g_watch.Open(context);   // what the window opened over
				s_watching = true;
			} else if (g_watch.ShouldClose(context)) {
				g_watch.Close();
				s_watching = false;
				RequestOpen(false, "the game state under the window changed");
			}
		} else if (s_watching) {
			g_watch.Close();
			s_watching = false;
		}
		gear::Tick();
		livepreview::GameTick();
	}

	bool HandleWindowMessage(HWND a_window, UINT a_message, WPARAM a_wparam, LPARAM a_lparam, LRESULT& a_result)
	{
		g_gameWindow.store(a_window);
		if (a_message == kOpenMessage) {
			SetOpenNow(a_wparam != 0);
			a_result = 0;
			return true;
		}
		if (a_message == WM_KEYDOWN && a_wparam == kToggleKey && !(a_lparam & (1 << 30)) && !g_failed.load()) {
			SetOpenNow(!g_open.load());
			a_result = 0;
			return true;
		}
		if (a_message == WM_KEYUP && a_wparam == kToggleKey) {
			a_result = 0;
			return true;
		}
		if ((a_message == WM_ACTIVATEAPP && !a_wparam) || a_message == WM_KILLFOCUS) {
			g_keys.LoseFocus();   // alt-tab: Windows sends no releases for held keys
			if (g_open.load()) QueueForImGui(a_message, a_wparam, a_lparam);
			return false;          // the game must see its focus change too
		}
		if (a_message == WM_SETFOCUS) {
			if (g_open.load()) QueueForImGui(a_message, a_wparam, a_lparam);
			return false;
		}
		if (!g_open.load() && !g_failed.load()) {   // the gear button (mouse only)
			if (a_message == WM_MOUSEMOVE) gear::Hover(gear::Hit(a_window, a_lparam));
			if ((a_message == WM_LBUTTONDOWN || a_message == WM_LBUTTONDBLCLK) && gear::Hit(a_window, a_lparam)) {
				gear::Hover(false);
				SetOpenNow(true);   // its release is hidden by the open gate (the game never saw the press)
				a_result = 0;
				return true;
			}
		}
		if (g_open.load() && a_message == WM_SETCURSOR && LOWORD(a_lparam) == HTCLIENT) {
			SetCursor(nullptr);   // ImGui draws its own cursor; the game's would show beside it
			a_result = TRUE;
			return true;
		}
		unsigned code = 0;
		const auto input = Classify(a_message, a_wparam, code);
		if (!input || !g_keys.Consume(*input, code)) return false;
		if (*input != ingame::Input::Raw) {
			QueueForImGui(a_message, a_wparam, a_lparam);
			a_result = 0;
		} else {
			a_result = DefWindowProcW(a_window, a_message, a_wparam, a_lparam);   // WM_INPUT must still be cleaned up
		}
		return true;
	}

	bool FilterPad(XINPUT_STATE& a_state)
	{
		ingame::Pad pad{ a_state.dwPacketNumber, a_state.Gamepad.wButtons, a_state.Gamepad.bLeftTrigger, a_state.Gamepad.bRightTrigger,
			a_state.Gamepad.sThumbLX, a_state.Gamepad.sThumbLY, a_state.Gamepad.sThumbRX, a_state.Gamepad.sThumbRY };
		const ingame::Pad original = pad;
		g_pad.Filter(g_open.load(), pad);
		const bool blanked = pad.packet != original.packet;
		if (blanked) {
			a_state.dwPacketNumber = pad.packet;
			a_state.Gamepad = XINPUT_GAMEPAD{};
		}
		return blanked;
	}
}
