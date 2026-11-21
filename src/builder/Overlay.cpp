// In-game Pack Builder overlay, Phase 0 (KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md).
// SPDX-License-Identifier: GPL-3.0-or-later
// Rendering: MinHook on dxgi.dll's CreateDXGIFactory/1/2; the factory's CreateSwapChain (slot 10) and
// CreateSwapChainForHwnd (IDXGIFactory2 slot 15) capture the D3D12 command queue; the swap chain's Present (8),
// ResizeBuffers (13) and Present1 (22) draw a Dear ImGui frame before the game's present. Nothing is created until the
// window is first opened with F11. Input: Menus Revamped's existing window procedure and XInputGetState chain call
// HandleWindowMessage / FilterPad; ingame::KeyGate and ingame::PadGate decide what the game must not see.
#include "builder/Builder.h"
#include "builder/InputGate.h"

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
		constexpr const char* kMarker = "MR-INGAME-BUILDER-P0";
		constexpr UINT kToggleKey = VK_F11;   // bound nowhere in the game (KB game-internals.md "Keyboard F-key bindings")
		constexpr UINT kSrvCount = 64;

		using CreateFactory_t = HRESULT(WINAPI*)(REFIID, void**);
		using CreateFactory2_t = HRESULT(WINAPI*)(UINT, REFIID, void**);
		using CreateSwapChain_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
		using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
		using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
		using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
		using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		CreateFactory_t          g_createFactory = nullptr;
		CreateFactory_t          g_createFactory1 = nullptr;
		CreateFactory2_t         g_createFactory2 = nullptr;
		CreateSwapChain_t        g_createSwapChain = nullptr;
		CreateSwapChainForHwnd_t g_createSwapChainForHwnd = nullptr;
		Present_t                g_present = nullptr;
		Present1_t               g_present1 = nullptr;
		ResizeBuffers_t          g_resizeBuffers = nullptr;

		std::atomic<bool> g_open{ false };
		std::atomic<bool> g_failed{ false };
		std::atomic<ID3D12CommandQueue*> g_queue{ nullptr };
		std::once_flag g_factoryOnce, g_factory2Once, g_swapOnce;

		// game window thread only
		ingame::KeyGate g_keys;
		ingame::PadGate g_pad;

		// window messages for ImGui, queued on the window thread and fed on the render thread before NewFrame
		struct Message { UINT msg; WPARAM w; LPARAM l; };
		std::mutex g_queueLock;
		std::vector<Message> g_messages;

		// --- D3D12 state (render thread) ---
		struct Frame
		{
			ID3D12CommandAllocator*     allocator = nullptr;
			ID3D12Resource*             buffer = nullptr;
			D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
			UINT64                      fenceValue = 0;
		};
		std::recursive_mutex        g_renderLock;
		bool                        g_ready = false;
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

		// --- the Phase 0 window ---
		float g_columnLine[3] = { 99 / 255.0f, 99 / 255.0f, 99 / 255.0f };
		std::atomic<int> g_retocState{ 0 };   // 0 idle, 1 running, 2 ok, 3 failed
		std::mutex g_retocTextLock;
		std::string g_retocText = "not run";

		template <class T>
		void Release(T*& a_object)
		{
			if (a_object) {
				a_object->Release();
				a_object = nullptr;
			}
		}

		std::filesystem::path PluginFolder()
		{
			wchar_t path[MAX_PATH]{};
			HMODULE self = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&PluginFolder), &self);
			GetModuleFileNameW(self, path, MAX_PATH);
			return std::filesystem::path(path).parent_path();
		}

		void ReadColumnLine()
		{
			std::error_code ec;
			const auto ini = PluginFolder() / L"MenusRevamped" / L"CustomColours.ini";
			if (!std::filesystem::is_regular_file(ini, ec)) return;
			wchar_t text[64]{};
			GetPrivateProfileStringW(L"Colours", L"columnLine", L"", text, 64, ini.c_str());
			int r = 0, g = 0, b = 0;
			if (swscanf_s(text, L"%d , %d , %d", &r, &g, &b) == 3 && r >= 0 && r < 256 && g >= 0 && g < 256 && b >= 0 && b < 256) {
				g_columnLine[0] = r / 255.0f;
				g_columnLine[1] = g / 255.0f;
				g_columnLine[2] = b / 255.0f;
			}
		}

		// One retoc to-legacy call on a background thread: proves the plugin can drive retoc without a window.
		void RunRetocTest()
		{
			int expected = 0;
			if (!g_retocState.compare_exchange_strong(expected, 1) && expected == 1) return;
			g_retocState = 1;
			std::thread([] {
				const auto start = std::chrono::steady_clock::now();
				std::string result;
				bool ok = false;
				try {
					const auto retoc = PluginFolder() / L"MenusRevamped" / L"PackBuilder" / L"retoc" / L"retoc.exe";
					wchar_t exe[MAX_PATH]{};
					GetModuleFileNameW(nullptr, exe, MAX_PATH);
					const auto paks = std::filesystem::path(exe).parent_path().parent_path().parent_path() / L"Content" / L"Paks";
					wchar_t temp[MAX_PATH]{};
					GetTempPathW(MAX_PATH, temp);
					const auto out = std::filesystem::path(temp) / L"MRPhase0Retoc";
					std::error_code ec;
					std::filesystem::remove_all(out, ec);
					if (!std::filesystem::is_regular_file(retoc, ec)) {
						result = "retoc.exe not found in MenusRevamped/PackBuilder/retoc";
					} else {
						std::wstring cmd = L"\"" + retoc.wstring() + L"\" to-legacy --no-shaders --version UE5_3 -f Art/UI/Common/T_Line_V \"" +
						                   paks.wstring() + L"\" \"" + out.wstring() + L"\"";
						STARTUPINFOW si{ sizeof(si) };
						PROCESS_INFORMATION pi{};
						if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
								nullptr, nullptr, &si, &pi)) {
							result = std::format("could not start retoc (error {})", GetLastError());
						} else {
							WaitForSingleObject(pi.hProcess, 120000);
							DWORD code = STILL_ACTIVE;
							GetExitCodeProcess(pi.hProcess, &code);
							if (code == STILL_ACTIVE) TerminateProcess(pi.hProcess, 1);
							CloseHandle(pi.hThread);
							CloseHandle(pi.hProcess);
							const auto uexp = out / L"OblivionRemastered" / L"Content" / L"Art" / L"UI" / L"Common" / L"T_Line_V.uexp";
							const auto size = std::filesystem::is_regular_file(uexp, ec) ? std::filesystem::file_size(uexp, ec) : 0;
							ok = code == 0 && size > 0;
							const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
							result = std::format("retoc exit {}, T_Line_V.uexp {} bytes, {} ms", code, size, ms);
						}
						std::filesystem::remove_all(out, ec);
					}
				} catch (const std::exception& e) {
					result = std::string("failed: ") + e.what();
				}
				logger::warn("{}: retoc test {} ({})", kMarker, ok ? "passed" : "failed", result);
				{
					std::scoped_lock lock(g_retocTextLock);
					g_retocText = result;
				}
				g_retocState = ok ? 2 : 3;
			}).detach();
		}

		void DrawWindow()
		{
			ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
			ImGui::SetNextWindowPos(ImVec2(80, 80), ImGuiCond_FirstUseEver);
			ImGui::Begin("Menus Revamped - Pack Builder (Phase 0 test)", nullptr, ImGuiWindowFlags_NoCollapse);
			ImGui::TextWrapped("Test window only: nothing here is saved or built. F11 closes it.");
			ImGui::Separator();
			ImGui::ColorEdit3("Column line", g_columnLine);
			ImGui::Spacing();
			const int state = g_retocState.load();
			ImGui::BeginDisabled(state == 1);
			if (ImGui::Button(state == 1 ? "Running retoc..." : "Test retoc")) RunRetocTest();
			ImGui::EndDisabled();
			std::string text;
			{
				std::scoped_lock lock(g_retocTextLock);
				text = g_retocText;
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(text.c_str());
			ImGui::End();
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
			const auto index = (a_cpu.ptr - g_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr) / g_srvStep;
			if (index < g_srvUsed.size()) g_srvUsed[index] = false;
		}

		void WaitIdle()
		{
			auto* queue = g_queue.load();
			if (!queue || !g_fence) return;
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

		bool Init(IDXGISwapChain* a_chain)
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
			chain1->GetHwnd(&g_window);
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

			IMGUI_CHECKVERSION();
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
			ImGui::StyleColorsDark();
			if (!ImGui_ImplWin32_Init(g_window)) return false;
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
			ReadColumnLine();
			logger::warn("{}: overlay ready ({} buffers, format {}, {}x{})", kMarker, desc.BufferCount, static_cast<int>(desc.Format), desc.Width, desc.Height);
			return true;
		}

		void Render(IDXGISwapChain* a_chain)
		{
			if (g_failed.load() || !g_open.load()) return;
			std::scoped_lock lock(g_renderLock);
			if (!g_ready) {
				if (!Init(a_chain)) {
					g_failed = true;
					g_open = false;
					logger::warn("{}: overlay could not start - it stays off this session", kMarker);
					return;
				}
				g_ready = true;
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

			ClipCursor(nullptr);
			ImGui::GetIO().MouseDrawCursor = true;
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();
			DrawWindow();
			ImGui::Render();

			auto& frame = g_frames[index];
			if (g_fence->GetCompletedValue() < frame.fenceValue && SUCCEEDED(g_fence->SetEventOnCompletion(frame.fenceValue, g_fenceEvent)))
				WaitForSingleObject(g_fenceEvent, 100);
			frame.allocator->Reset();
			g_list->Reset(frame.allocator, nullptr);
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

		HRESULT STDMETHODCALLTYPE OnResizeBuffers(IDXGISwapChain* a_chain, UINT a_count, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags)
		{
			std::scoped_lock lock(g_renderLock);
			if (!g_ready) return g_resizeBuffers(a_chain, a_count, a_width, a_height, a_format, a_flags);
			WaitIdle();
			ReleaseBuffers();
			const HRESULT hr = g_resizeBuffers(a_chain, a_count, a_width, a_height, a_format, a_flags);
			if (SUCCEEDED(hr)) {
				if (a_count != 0 && a_count != g_frames.size()) {
					g_failed = true;
					g_open = false;
					logger::warn("{}: swap chain buffer count changed ({} -> {}) - overlay off until restart", kMarker, g_frames.size(), a_count);
				} else if (!AcquireBuffers(a_chain)) {
					g_failed = true;
					g_open = false;
					logger::warn("{}: back buffers not available after resize - overlay off until restart", kMarker);
				}
			}
			return hr;
		}

		void HookSwapChain(IUnknown* a_chain)
		{
			std::call_once(g_swapOnce, [a_chain] {
				auto** vtable = *reinterpret_cast<void***>(a_chain);
				const bool ok = MH_CreateHook(vtable[8], &OnPresent, reinterpret_cast<void**>(&g_present)) == MH_OK &&
				                MH_CreateHook(vtable[13], &OnResizeBuffers, reinterpret_cast<void**>(&g_resizeBuffers)) == MH_OK &&
				                MH_CreateHook(vtable[22], &OnPresent1, reinterpret_cast<void**>(&g_present1)) == MH_OK &&
				                MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
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
			CaptureQueue(a_device);
			const HRESULT hr = g_createSwapChain(a_factory, a_device, a_desc, a_out);
			if (SUCCEEDED(hr) && a_out && *a_out) HookSwapChain(*a_out);
			return hr;
		}

		HRESULT STDMETHODCALLTYPE OnCreateSwapChainForHwnd(IDXGIFactory2* a_factory, IUnknown* a_device, HWND a_window, const DXGI_SWAP_CHAIN_DESC1* a_desc,
			const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_full, IDXGIOutput* a_output, IDXGISwapChain1** a_out)
		{
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
		logger::warn("{}: in-game builder installed (F11), {} of 3 dxgi factory hooks", kMarker, hooked);
	}

	bool IsOpen() { return g_open.load(); }

	bool HandleWindowMessage(HWND a_window, UINT a_message, WPARAM a_wparam, LPARAM a_lparam, LRESULT& a_result)
	{
		if (a_message == WM_KEYDOWN && a_wparam == kToggleKey && !(a_lparam & (1 << 30)) && !g_failed.load()) {
			const bool open = !g_open.load();
			g_keys.SetOpen(open);
			g_open = open;
			if (!open) {
				std::scoped_lock lock(g_queueLock);
				g_messages.clear();
			}
			a_result = 0;
			return true;
		}
		if (a_message == WM_KEYUP && a_wparam == kToggleKey) {
			a_result = 0;
			return true;
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
			std::scoped_lock lock(g_queueLock);
			if (g_messages.size() < 512) g_messages.push_back({ a_message, a_wparam, a_lparam });
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
