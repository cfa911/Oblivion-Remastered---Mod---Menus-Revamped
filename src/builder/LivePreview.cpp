// In-game Pack Builder Phase 4a: live art preview (see LivePreview.h). Three threads:
//   worker (one, below normal priority): stock cache, colour values, which textures changed, their bake (LivePreviewCore.h)
//   game thread (GameTick): UTexture2D -> ID3D12Resource, checked against the baked size and format
//   render thread (Record/Submitted, the overlay's Present): barrier 0xC0 -> COPY_DEST -> 0xC0 and one copy per mip from an
//   upload buffer, released once the fence passes
// Facts used: KB docs/reverse-engineering/dark-mode-ui.md "Loaded UI textures in GPU memory" (game 1.512.105.0).
// Phase 4b: every colour-file key is live; widget colour literals go to LiveWidgets.cpp; GameTick keeps re-resolving every
// preview made this session (g_latest, a few assets per tick) so a menu opened later is painted too; a streamed texture gets
// the package mips from its resident top size (MipWindow); the overlay records the copies even while the window is closed.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "builder/LivePreview.h"

#include "builder/LivePreviewCore.h"
#include "builder/LiveWidgets.h"
#include "builder/PackJob.h"
#include "builder/PackStock.h"

#include <d3d12.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace builder::livepreview
{
	namespace
	{
		namespace fs = std::filesystem;
		namespace live = pack::live;

		constexpr const char* kMarker = "MR-LIVE-PREVIEW";

		// ---- worker ----
		std::mutex                    g_lock;   // g_plugins, g_status, g_baked, g_latest
		fs::path                      g_plugins;
		live::LatestWins<std::string> g_requests;
		std::mutex                    g_waitLock;   // with g_wake: a request posted under it is never missed
		std::condition_variable       g_wake;
		std::once_flag                g_started;
		std::atomic<bool>             g_cancel{ false };
		std::atomic<bool>             g_preparing{ false };
		std::string                   g_status;
		bool                          g_statusError = false;

		using PreviewPtr = std::shared_ptr<const live::Preview>;
		struct Versioned
		{
			PreviewPtr    preview;
			std::uint64_t version = 0;
		};
		std::uint64_t                    g_version = 0;   // under g_lock
		std::map<std::string, Versioned> g_baked;    // new previews for the game thread (latest per asset)
		std::map<std::string, Versioned> g_latest;   // every preview made this session (re-painted when a menu opens later)

		std::string AssetNameOf(const std::string& a_asset) { return pack::AssetName(a_asset); }

		void SetStatus(std::string a_text, bool a_error)
		{
			std::scoped_lock guard(g_lock);
			g_status = std::move(a_text);
			g_statusError = a_error;
		}

		std::string ReadText(const fs::path& a_path)
		{
			std::ifstream     in(a_path, std::ios::binary);
			std::stringstream s;
			s << in.rdbuf();
			return s.str();
		}

		// the pak set retoc reads: every file of the Paks folder (not its sub-folders) with its size and write time
		std::string PaksStamp(const fs::path& a_paks)
		{
			std::error_code          ec;
			std::vector<std::string> rows;
			for (const auto& e : fs::directory_iterator(a_paks, ec)) {
				if (!e.is_regular_file(ec)) continue;
				const auto time = e.last_write_time(ec).time_since_epoch().count();
				rows.push_back(e.path().filename().string() + " " + std::to_string(e.file_size(ec)) + " " + std::to_string(time));
			}
			std::sort(rows.begin(), rows.end());
			std::string out;
			for (const auto& r : rows) out += r + ";";
			return out;
		}

		struct Session
		{
			fs::path                         plugins, menus, cache;
			pack::PackData                   data;
			std::unique_ptr<pack::StockFolder> stock;
			pack::BakeInputs                 inputs;
			bool                             ready = false;
		};

		// the data files and the stock cache (extracted once, reused while its stamp matches); false + the status on a failure
		bool Prepare(Session& a_s)
		{
			std::error_code ec;
			const auto      builder = a_s.plugins / L"MenusRevamped" / L"PackBuilder";
			a_s.menus = builder / L"menus";
			a_s.cache = builder / L"PreviewCache";
			std::vector<std::pair<std::string, std::string>> files;
			for (const auto& e : fs::directory_iterator(a_s.menus, ec))
				if (e.path().extension() == L".json") files.emplace_back(e.path().filename().string(), ReadText(e.path()));
			std::string error;
			if (files.empty() || !pack::LoadPackData(files, a_s.data, error)) {
				SetStatus("Live preview off: " + (files.empty() ? "no data files in " + a_s.menus.string() : error), true);
				return false;
			}
			const auto names = pack::NeededNames(a_s.data, false);
			const auto stamp = live::CacheStamp(names, PaksStamp(pack::job::PaksFolder(a_s.plugins)));
			const auto legacy = a_s.cache / L"legacy", stampFile = a_s.cache / L"stamp.txt";
			if (!(fs::is_directory(legacy, ec) && fs::is_regular_file(stampFile, ec) && ReadText(stampFile) == stamp)) {
				g_preparing = true;
				SetStatus("Live preview: reading the game's art once (first use only)...", false);
				const auto started = std::chrono::steady_clock::now();
				const auto fresh = a_s.cache / L"legacy.new";
				fs::remove_all(pack::StockFolder::Long(fresh), ec);
				fs::remove(stampFile, ec);
				fs::create_directories(a_s.cache, ec);
				const bool ok = pack::job::ExtractStock(a_s.plugins, names, fresh, g_cancel, error);
				g_preparing = false;
				if (!ok) {
					fs::remove_all(pack::StockFolder::Long(fresh), ec);
					if (g_cancel.load()) SetStatus("", false);   // the window closed: the next edit starts again
					else {
						SetStatus("Live preview off: " + error, true);
						logger::warn("{}: stock extraction failed: {}", kMarker, error);
					}
					return false;
				}
				fs::remove_all(pack::StockFolder::Long(legacy), ec);
				fs::rename(pack::StockFolder::Long(fresh), pack::StockFolder::Long(legacy), ec);
				if (ec) {
					SetStatus("Live preview off: could not move the stock cache into place (" + ec.message() + ")", true);
					return false;
				}
				std::ofstream(stampFile, std::ios::binary | std::ios::trunc) << stamp;
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
				logger::info("{}: stock cache of {} assets ready in {} ms", kMarker, names.size(), ms);
			}
			a_s.stock = std::make_unique<pack::StockFolder>(legacy, a_s.menus);
			a_s.inputs = a_s.stock->Inputs();
			a_s.ready = true;
			SetStatus("", false);
			return true;
		}

		void Worker()
		{
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
			Session                   s;
			pack::Values              last, base;   // the newest values; the values at the first text (what the screen showed)
			bool                      haveLast = false;
			std::set<std::string>     dirty;   // assets still to bake with the newest values
			std::set<std::string>     warned;
			bool                      givenUp = false;   // the stock cache failed (not cancelled): no new try this session
			for (;;) {
				std::optional<std::string> text;
				{
					std::unique_lock wait(g_waitLock);
					g_wake.wait(wait, [] { return g_requests.Pending(); });
				}
				text = g_requests.Take();
				if (!text) continue;
				pack::Values values;
				std::string  error;
				if (!pack::PackValues(&*text, values, error)) continue;   // the window refuses a bad file before this
				widgets::Skin(*text);                                     // our own run-time tints (no stock files needed)
				if (!haveLast) {                                          // the first text is what the screen shows
					last = values;
					base = std::move(values);
					haveLast = true;
					continue;
				}
				for (const auto& k : live::ChangedKeys(last, values, {})) dirty.insert(k);   // every key: roles, Advanced, Runtime
				last = values;
				if (dirty.empty()) continue;
				{
					std::scoped_lock guard(g_lock);
					s.plugins = g_plugins;
				}
				if (!s.ready) {
					if (givenUp) continue;
					g_cancel = false;
					if (!Prepare(s)) {   // the changed keys stay in dirty for the next edit
						givenUp = !g_cancel.load();
						continue;
					}
				}
				// changed keys -> widget literals at once (cheap), then texture rows baked one by one with the newest values; a newer
				// edit adds its keys and continues
				std::vector<std::string> keys(dirty.begin(), dirty.end());
				dirty.clear();
				auto literals = [&](const std::vector<std::string>& a_keys) {
					std::vector<widgets::Paint>               paints;
					std::map<std::string, pack::PackageFiles> files;
					for (const auto& l : live::AffectedLiterals(s.data, a_keys)) {
						if (l.owner.empty()) continue;   // a material literal that names no parameter (steelGlow's shader map)
						auto it = files.find(l.asset);
						if (it == files.end()) {
							pack::PackageFiles f;
							if (!s.stock->Read(l.asset, f)) f = {};
							it = files.emplace(l.asset, std::move(f)).first;
						}
						widgets::Paint paint{ l };
						if (live::StockLiteral(l, it->second.uexp, paint.stock) && live::LiteralColour(l, last, paint.stock[3], paint.colour) &&
							live::LiteralColour(l, base, paint.stock[3], paint.baseline))
							paints.push_back(std::move(paint));
						else if (warned.insert(l.asset + "|" + std::to_string(l.offset)).second)
							logger::warn("{}: {} {} not previewed (the stock literal is not at its offset)", kMarker, AssetNameOf(l.asset), l.owner);
					}
					if (!paints.empty()) widgets::Post(std::move(paints));
				};
				literals(keys);
				std::vector<std::string> later;   // keys of newer edits taken during the bakes
				auto targets = live::AffectedTextures(s.data, keys);
				for (size_t i = 0; i < targets.size(); ++i) {
					if (auto newer = g_requests.Take()) {
						pack::Values next;
						if (pack::PackValues(&*newer, next, error)) {
							widgets::Skin(*newer);
							const auto more = live::ChangedKeys(last, next, {});
							later.insert(later.end(), more.begin(), more.end());
							last = std::move(next);
							for (const auto& t : live::AffectedTextures(s.data, more))   // re-bake with the newer values, even if done already
								if (std::none_of(targets.begin() + static_cast<std::ptrdiff_t>(i), targets.end(), [&](const live::Target& a) { return a.asset == t.asset; }))
									targets.push_back(t);
						}
					}
					const auto&       t = targets[i];
					pack::PackageFiles stock;
					auto              preview = std::make_shared<live::Preview>();
					std::string       reason;
					if (!s.stock->Read(t.asset, stock)) reason = "not in the stock cache";
					else if (live::BakePreview(t, stock, last, s.data, s.inputs, *preview, reason)) {
						std::scoped_lock guard(g_lock);
						const Versioned v{ preview, ++g_version };
						g_baked[t.asset] = v;
						g_latest[t.asset] = v;
						continue;
					}
					if (warned.insert(t.asset).second) logger::warn("{}: {} not previewed: {}", kMarker, t.asset, reason);
				}
				if (!later.empty()) literals(later);
			}
		}

		// ---- game thread: the loaded texture's GPU resource ----
		bool SafeRead(const void* a_from, void* a_to, std::size_t a_size)
		{
			__try {
				std::memcpy(a_to, a_from, a_size);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		template <class T> bool ReadAt(std::uintptr_t a_base, std::uint32_t a_offset, T& a_out)
		{
			return a_base >= 0x10000 && SafeRead(reinterpret_cast<const void*>(a_base + a_offset), &a_out, sizeof(T));
		}

		bool InD3D12Module(std::uintptr_t a_p)
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(a_p), &module))
				return false;
			wchar_t name[MAX_PATH]{};
			GetModuleFileNameW(module, name, MAX_PATH);
			std::wstring n(name);
			for (auto& c : n) c = static_cast<wchar_t>(towlower(c));
			return n.find(L"d3d12") != std::wstring::npos;
		}

		// UTexture2D +0x110 render resource -> +0x10 RHI texture -> +0xB8 resource holder -> +0x20 ID3D12Resource; the holder's
		// tracked state (+0xC0) must be 0xC0 (pixel | non-pixel shader resource), the state the copy's barriers restore
		ID3D12Resource* ResourceOf(UE::UObject* a_texture)
		{
			std::uintptr_t render = 0, rhi = 0, holder = 0, resource = 0, vtable = 0;
			std::uint32_t  state = 0;
			const auto     base = reinterpret_cast<std::uintptr_t>(a_texture);
			if (!ReadAt(base, 0x110, render) || !ReadAt(render, 0x10, rhi) || !ReadAt(rhi, 0xB8, holder) || !ReadAt(holder, 0x20, resource) ||
				!ReadAt(holder, 0xC0, state) || state != 0xC0 || !ReadAt(resource, 0, vtable) || !InD3D12Module(vtable))
				return nullptr;
			ID3D12Resource* out = nullptr;
			if (FAILED(reinterpret_cast<IUnknown*>(resource)->QueryInterface(IID_PPV_ARGS(&out)))) return nullptr;
			return out;   // AddRef'd
		}

		struct Upload
		{
			ID3D12Resource* resource = nullptr;   // AddRef'd
			PreviewPtr      preview;
			UINT            first = 0;            // the package mip copied into the resource's mip 0 (a streamed texture: > 0)
			UINT            mips = 0;             // mips written (the resource may have more: those keep their pixels)
		};
		std::mutex                       g_uploadLock;   // g_uploads, g_inFlight
		std::map<ID3D12Resource*, Upload> g_uploads;     // waiting for the render thread (latest per resource)

		struct InFlight
		{
			ID3D12Resource*    resource = nullptr;
			ID3D12Resource*    staging = nullptr;
			unsigned long long fence = ~0ull;   // set by Submitted
		};
		std::vector<InFlight> g_inFlight;

		std::wstring Wide(const std::string& a_ascii) { return std::wstring(a_ascii.begin(), a_ascii.end()); }   // asset paths are ASCII

		// game thread: what each texture was last painted with, and the rolling cursor over g_latest (menus opened later)
		std::map<std::string, live::PaintMemo> g_memo;
		std::string                            g_cursor;
		constexpr size_t                       kSweepPerTick = 8;   // ~380 previews: every asset looked at within a second

		// a preview into its loaded texture's GPU resource, unless that resource already shows it
		void Queue(const std::string& a_asset, const Versioned& a_v)
		{
			auto* texture = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, Wide(live::ObjectPath(a_asset)).c_str());
			if (!texture) return;   // not loaded: the sweep paints it once its menu loads it
			auto* resource = ResourceOf(texture);
			if (!resource) return;
			auto& memo = g_memo[a_asset];
			const auto& p = *a_v.preview;
			const auto  desc = resource->GetDesc();
			int         count = 0;
			const int   first = live::MipWindow(p.width, p.height, static_cast<int>(p.mips.size()), static_cast<int>(desc.Width), static_cast<int>(desc.Height), desc.MipLevels, count);
			if (!live::NeedsPaint(memo, resource, a_v.version) || desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.DepthOrArraySize != 1 ||
				!live::FormatMatches(p.format, static_cast<unsigned>(desc.Format)) || first < 0) {
				resource->Release();
				return;
			}
			memo = { resource, a_v.version };
			Upload upload{ resource, a_v.preview, static_cast<UINT>(first), static_cast<UINT>(count) };
			std::scoped_lock guard(g_uploadLock);
			if (auto it = g_uploads.find(resource); it != g_uploads.end()) {
				it->second.resource->Release();   // the older upload of the same texture: replaced
				it->second = upload;
			} else
				g_uploads.emplace(resource, upload);
		}
	}

	void SetPluginFolder(const std::filesystem::path& a_plugins)
	{
		std::scoped_lock guard(g_lock);
		g_plugins = a_plugins;
	}

	void Colours(const std::string& a_colourFile)
	{
		std::call_once(g_started, [] { std::thread(Worker).detach(); });
		{
			std::scoped_lock guard(g_waitLock);
			g_requests.Post(a_colourFile);
		}
		g_wake.notify_one();
	}

	void WindowClosed()
	{
		if (g_preparing.load()) g_cancel = true;
	}

	std::string Status()
	{
		std::scoped_lock guard(g_lock);
		return g_status;
	}

	bool StatusIsError()
	{
		std::scoped_lock guard(g_lock);
		return g_statusError;
	}

	void GameTick()
	{
		widgets::Tick();
		std::map<std::string, Versioned> baked, sweep;
		{
			std::scoped_lock guard(g_lock);
			baked.swap(g_baked);
			// a few previews per tick, round robin: a texture whose menu was opened after the edit gets a new resource
			auto it = g_latest.upper_bound(g_cursor);
			for (size_t n = 0; n < kSweepPerTick && !g_latest.empty(); ++n) {
				if (it == g_latest.end()) it = g_latest.begin();
				sweep.insert(*it);
				g_cursor = it->first;
				++it;
				if (sweep.size() == g_latest.size()) break;
			}
		}
		for (const auto& [asset, v] : baked) Queue(asset, v);
		for (const auto& [asset, v] : sweep)
			if (!baked.count(asset)) Queue(asset, v);
	}

	bool HasWork()
	{
		std::scoped_lock guard(g_uploadLock);
		return !g_uploads.empty() || !g_inFlight.empty();
	}

	void Record(ID3D12Device* a_device, ID3D12GraphicsCommandList* a_list, unsigned long long a_completedFence)
	{
		std::map<ID3D12Resource*, Upload> uploads;
		{
			std::scoped_lock guard(g_uploadLock);
			std::erase_if(g_inFlight, [&](const InFlight& f) {
				if (f.fence > a_completedFence) return false;
				f.staging->Release();
				f.resource->Release();
				return true;
			});
			uploads.swap(g_uploads);
		}
		for (auto& [resource, upload] : uploads) {
			const auto& p = *upload.preview;
			const auto  desc = resource->GetDesc();
			const UINT  n = upload.mips;
			const UINT  first = upload.first;
			std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(n);
			std::vector<UINT>   rows(n);
			std::vector<UINT64> rowSizes(n);
			UINT64              total = 0;
			a_device->GetCopyableFootprints(&desc, 0, n, 0, footprints.data(), rows.data(), rowSizes.data(), &total);
			bool fits = n > 0;
			fits = fits && first + n <= p.mips.size();
			for (UINT i = 0; i < n && fits; ++i) fits = rowSizes[i] == p.mips[first + i].rowBytes && rows[i] == static_cast<UINT>(p.mips[first + i].rows);
			ID3D12Resource* staging = nullptr;
			if (fits) {
				D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_UPLOAD };
				D3D12_RESOURCE_DESC   buffer{};
				buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
				buffer.Width = total;
				buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
				buffer.SampleDesc.Count = 1;
				buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
				fits = SUCCEEDED(a_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)));
			}
			std::uint8_t* mapped = nullptr;
			if (fits && FAILED(staging->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) fits = false;
			if (!fits) {
				logger::warn("{}: {} not uploaded (GPU layout differs from the baked mips or no upload buffer)", kMarker, p.asset);
				if (staging) staging->Release();
				resource->Release();
				continue;
			}
			for (UINT i = 0; i < n; ++i) {
				const auto& m = p.mips[first + i];
				for (int r = 0; r < m.rows; ++r)
					std::memcpy(mapped + footprints[i].Offset + UINT64(r) * footprints[i].Footprint.RowPitch, p.body.data() + m.offset + size_t(r) * m.rowBytes, m.rowBytes);
			}
			staging->Unmap(0, nullptr);
			D3D12_RESOURCE_BARRIER barrier{};
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			barrier.Transition.pResource = resource;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			a_list->ResourceBarrier(1, &barrier);
			for (UINT i = 0; i < n; ++i) {
				D3D12_TEXTURE_COPY_LOCATION dst{ resource, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
				dst.SubresourceIndex = i;
				D3D12_TEXTURE_COPY_LOCATION src{ staging, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT };
				src.PlacedFootprint = footprints[i];
				a_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
			}
			std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
			a_list->ResourceBarrier(1, &barrier);
			std::scoped_lock guard(g_uploadLock);
			g_inFlight.push_back({ resource, staging });
		}
	}

	void Submitted(unsigned long long a_fence)
	{
		std::scoped_lock guard(g_uploadLock);
		for (auto& f : g_inFlight)
			if (f.fence == ~0ull) f.fence = a_fence;
	}

	void ReleaseAll()
	{
		std::scoped_lock guard(g_uploadLock);
		for (auto& f : g_inFlight) {
			f.staging->Release();
			f.resource->Release();
		}
		g_inFlight.clear();
	}
}
