// Style preview of the in-game Pack Builder window: draws src/builder/Shell.cpp with the same Dear ImGui DX12 backend the
// plugin uses, on a WARP (software) device, off screen, over a game screenshot laid into a back buffer of the game's format
// (so blending matches the game), and writes the result as raw RGBA for tools/builder_preview/compose.py.
// Developer tool only (owner rule: preview every styling change before deploy).
// Build: tools/builder_preview/build.bat. Usage: preview.exe <width> <height> <backdrop.rgba> <out.rgba>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>

#include "builder/Shell.h"

namespace
{
	ID3D12DescriptorHeap* g_srvHeap = nullptr;
	UINT g_srvStep = 0, g_srvNext = 0;

	void AllocSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* a_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* a_gpu)
	{
		a_cpu->ptr = g_srvHeap->GetCPUDescriptorHandleForHeapStart().ptr + SIZE_T(g_srvNext) * g_srvStep;
		a_gpu->ptr = g_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr + UINT64(g_srvNext) * g_srvStep;
		++g_srvNext;
	}
	void FreeSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {}

	int Fail(const char* a_what)
	{
		std::fprintf(stderr, "preview: %s failed\n", a_what);
		return 1;
	}
}

int main(int argc, char** argv)
{
	if (argc < 5) return Fail("arguments (width height backdrop.rgba out.rgba)");
	const UINT width = static_cast<UINT>(std::atoi(argv[1])), height = static_cast<UINT>(std::atoi(argv[2]));
	IDXGIFactory4* factory = nullptr;
	IDXGIAdapter* warp = nullptr;
	ID3D12Device* device = nullptr;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) ||
		FAILED(D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
		return Fail("WARP device");
	D3D12_COMMAND_QUEUE_DESC qd{ D3D12_COMMAND_LIST_TYPE_DIRECT };
	ID3D12CommandQueue* queue = nullptr;
	ID3D12CommandAllocator* allocator = nullptr;
	ID3D12GraphicsCommandList* list = nullptr;
	ID3D12Fence* fence = nullptr;
	device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));
	device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator));
	device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr, IID_PPV_ARGS(&list));
	list->Close();
	device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	UINT64 fenceValue = 0;
	auto wait = [&] {
		queue->Signal(fence, ++fenceValue);
		fence->SetEventOnCompletion(fenceValue, event);
		WaitForSingleObject(event, INFINITE);
	};

	// the game's back buffer format (KB techniques/in-game-imgui-overlay.md: R10G10B10A2_UNORM); read back and widened to 8 bits
	const DXGI_FORMAT format = DXGI_FORMAT_R10G10B10A2_UNORM;
	D3D12_HEAP_PROPERTIES defaultHeap{ D3D12_HEAP_TYPE_DEFAULT }, readbackHeap{ D3D12_HEAP_TYPE_READBACK };
	D3D12_RESOURCE_DESC td{ D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, width, height, 1, 1, format, { 1, 0 }, D3D12_TEXTURE_LAYOUT_UNKNOWN,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET };
	D3D12_CLEAR_VALUE clear{ format, { 0, 0, 0, 0 } };
	ID3D12Resource* target = nullptr;
	if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, &clear, IID_PPV_ARGS(&target))))
		return Fail("render target");
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT64 total = 0;
	device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, nullptr, nullptr, &total);
	D3D12_RESOURCE_DESC bd{ D3D12_RESOURCE_DIMENSION_BUFFER, 0, total, 1, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D12_TEXTURE_LAYOUT_ROW_MAJOR };
	ID3D12Resource* readback = nullptr;
	device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));

	// the backdrop, packed to 10 bits per channel in an upload buffer with the texture's footprint
	D3D12_HEAP_PROPERTIES uploadHeap{ D3D12_HEAP_TYPE_UPLOAD };
	ID3D12Resource* upload = nullptr;
	device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
	{
		std::vector<std::uint8_t> backdrop(std::size_t(width) * height * 4);
		FILE* in = nullptr;
		if (fopen_s(&in, argv[3], "rb") || !in || std::fread(backdrop.data(), 1, backdrop.size(), in) != backdrop.size()) return Fail("reading the backdrop");
		std::fclose(in);
		std::uint8_t* up = nullptr;
		upload->Map(0, nullptr, reinterpret_cast<void**>(&up));
		for (UINT y = 0; y < height; ++y) {
			auto* row = reinterpret_cast<std::uint32_t*>(up + footprint.Offset + std::size_t(y) * footprint.Footprint.RowPitch);
			for (UINT x = 0; x < width; ++x) {
				const auto* i = &backdrop[(std::size_t(y) * width + x) * 4];
				row[x] = (std::uint32_t(i[0]) * 1023 / 255) | (std::uint32_t(i[1]) * 1023 / 255) << 10 | (std::uint32_t(i[2]) * 1023 / 255) << 20 | 3u << 30;
			}
		}
		upload->Unmap(0, nullptr);
	}
	auto barrier = [&](D3D12_RESOURCE_STATES a_from, D3D12_RESOURCE_STATES a_to) {
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = target;
		b.Transition.Subresource = 0;
		b.Transition.StateBefore = a_from;
		b.Transition.StateAfter = a_to;
		list->ResourceBarrier(1, &b);
	};

	ID3D12DescriptorHeap* rtvHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC rd{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 };
	D3D12_DESCRIPTOR_HEAP_DESC sd{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE };
	device->CreateDescriptorHeap(&rd, IID_PPV_ARGS(&rtvHeap));
	device->CreateDescriptorHeap(&sd, IID_PPV_ARGS(&g_srvHeap));
	g_srvStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
	device->CreateRenderTargetView(target, nullptr, rtv);

	ImGui::CreateContext();
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
	io.DeltaTime = 1.0f / 60.0f;
	builder::shell::LoadFont();
	builder::shell::ApplyStyle(static_cast<float>(height));
	ImGui_ImplDX12_InitInfo info;
	info.Device = device;
	info.CommandQueue = queue;
	info.NumFramesInFlight = 1;
	info.RTVFormat = format;
	info.SrvDescriptorHeap = g_srvHeap;
	info.SrvDescriptorAllocFn = &AllocSrv;
	info.SrvDescriptorFreeFn = &FreeSrv;
	if (!ImGui_ImplDX12_Init(&info)) return Fail("ImGui DX12 backend");

	for (int frame = 0; frame < 3; ++frame) {   // a few frames so the window settles at its appearing size and position
		ImGui_ImplDX12_NewFrame();
		ImGui::NewFrame();
		builder::shell::Draw();
		ImGui::Render();
		allocator->Reset();
		list->Reset(allocator, nullptr);
		if (frame > 0) barrier(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
		{
			D3D12_TEXTURE_COPY_LOCATION dst{ target, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
			D3D12_TEXTURE_COPY_LOCATION src{ upload, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
			src.PlacedFootprint = footprint;
			list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);   // the game frame under the window, fresh each frame
		}
		barrier(D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
		list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		list->SetDescriptorHeaps(1, &g_srvHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), list);
		if (frame == 2) {
			barrier(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
			D3D12_TEXTURE_COPY_LOCATION dst{ readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
			dst.PlacedFootprint = footprint;
			D3D12_TEXTURE_COPY_LOCATION src{ target, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
			list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}
		list->Close();
		ID3D12CommandList* lists[] = { list };
		queue->ExecuteCommandLists(1, lists);
		wait();
	}

	std::uint8_t* mapped = nullptr;
	readback->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	std::vector<std::uint8_t> rgba(std::size_t(width) * height * 4);
	for (UINT y = 0; y < height; ++y) {
		const auto* row = reinterpret_cast<const std::uint32_t*>(mapped + footprint.Offset + std::size_t(y) * footprint.Footprint.RowPitch);
		for (UINT x = 0; x < width; ++x) {
			const std::uint32_t p = row[x];
			auto* o = &rgba[(std::size_t(y) * width + x) * 4];
			o[0] = static_cast<std::uint8_t>(((p >> 0) & 1023) * 255 / 1023);
			o[1] = static_cast<std::uint8_t>(((p >> 10) & 1023) * 255 / 1023);
			o[2] = static_cast<std::uint8_t>(((p >> 20) & 1023) * 255 / 1023);
			o[3] = 255;
		}
	}
	readback->Unmap(0, nullptr);
	FILE* out = nullptr;
	if (fopen_s(&out, argv[4], "wb") || !out) return Fail("writing the output");
	std::fwrite(rgba.data(), 1, rgba.size(), out);
	std::fclose(out);
	ImGui_ImplDX12_Shutdown();
	ImGui::DestroyContext();
	std::printf("preview: %ux%u written\n", width, height);
	return 0;
}
