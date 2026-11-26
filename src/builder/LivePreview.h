#pragma once
// In-game Pack Builder Phase 4a: live art preview while the window is open (session only; the Custom preset stays baked,
// owner decision 4). A colour edit in a live role re-bakes every texture whose recipe reads it (the build's own bake,
// LivePreviewCore.h) on a worker thread, latest edit wins; the game thread finds each loaded UTexture2D's ID3D12Resource
// (validated by its desc); the render thread copies the baked mips into it on the game's queue (KB
// docs/reverse-engineering/dark-mode-ui.md "Loaded UI textures in GPU memory"). The stock pixels come from a retoc extraction
// cached in MenusRevamped/PackBuilder/PreviewCache, made once in the background on the first edit and reused afterwards.
// Log marker MR-LIVE-PREVIEW.
#include <filesystem>
#include <string>

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace builder::livepreview
{
	void SetPluginFolder(const std::filesystem::path& a_plugins);   // before the first Colours

	// any thread: the window's colour file text after an edit (and at load: the first text is the baseline, nothing is painted)
	void Colours(const std::string& a_colourFile);

	// any thread: the window closed; a running stock extraction stops (the next edit starts it again)
	void WindowClosed();

	// any thread: the window's status line for the preview ("" = nothing to say)
	std::string Status();
	bool        StatusIsError();

	void GameTick();   // game thread, from builder::Tick: baked previews -> the loaded textures' GPU resources

	// render thread, the overlay's command list open: release finished uploads, record the copies of the textures found
	void Record(ID3D12Device* a_device, ID3D12GraphicsCommandList* a_list, unsigned long long a_completedFence);
	void Submitted(unsigned long long a_fence);   // render thread: the recorded list was signalled with this fence value
	void ReleaseAll();                            // render thread, the queue idle (the overlay drops its device objects)
}
