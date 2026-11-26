// In-game Pack Builder window contents, Phase 2: the builder UI (KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md).
// SPDX-License-Identifier: GPL-3.0-or-later
// A port of tools/pack_builder/window.py without Build (Phase 3): tabs Colours (roles, Advanced = each element), Text, Rows,
// Runtime; Save, Reset to Dark, Vanilla colours, Open log. The model (builder/ColourModel.h) reads and writes the same
// CustomColours.ini as the Python builder. Keyboard and mouse only (owner, 2026-11-21). Colours follow the Dark preset of Menus
// Revamped (core/UiTheme.h kDark: near-black warm paper, bronze border, parchment ink, amber focus). No game state is touched.
#include "builder/Shell.h"

#include "builder/ColourModel.h"
#include "builder/PackJob.h"
#include "builder/PackStatus.h"

#include <imgui.h>

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace builder::shell
{
	namespace
	{
		namespace fs = std::filesystem;

		const ImVec4 kInk(0.94f, 0.89f, 0.77f, 1.0f);
		const ImVec4 kInkDim(0.60f, 0.54f, 0.43f, 1.0f);
		const ImVec4 kAmber(0.82f, 0.58f, 0.22f, 1.0f);
		const ImVec4 kError(0.92f, 0.45f, 0.36f, 1.0f);

		constexpr const char* kSaveNote = "Save writes CustomColours.ini. Runtime colours apply at the next game start; baked colours and sizes need a pack build.";

		struct State
		{
			fs::path                   plugins;
			model::ColourModel         model;
			bool                       loaded = false;
			std::string                broken;      // the colour file cannot be read: the message
			std::string                saved;       // the file text as last read or written (unsaved changes = Render() differs)
			bool                       dirty = false;
			std::string                status = kSaveNote;
			bool                       buildReported = true;   // the last build's result is in the status line
			std::string                packStatus;             // "Build pack to apply: ..." against the installed manifests (full)
			std::string                packShort;              // the same, at most three names (the window line; the tooltip has all)
			bool                       statusError = false;
			bool                       advanced = false;
			int                        previewTab = -1;
			bool                       askVanilla = false;
			LiveHooks                  live;
		};

		State& S()
		{
			static State state;
			return state;
		}

		fs::path ColourFile() { return S().plugins / L"MenusRevamped" / L"CustomColours.ini"; }
		fs::path PluginIni() { return S().plugins / L"MenusRevamped.ini"; }
		fs::path BuilderLog() { return S().plugins / L"MenusRevamped" / L"PackBuilder" / L"PackBuilder.log"; }   // the in-game Build writes it
		fs::path PackManifest(const wchar_t* a_pack) { return S().plugins / L"MenusRevamped" / L"CustomPack" / (std::wstring(a_pack) + L".json"); }

		bool ReadFile(const fs::path& a_path, std::string& a_text)
		{
			a_text.clear();
			std::error_code ec;
			if (!fs::is_regular_file(a_path, ec)) return false;
			std::ifstream in(a_path, std::ios::binary);
			std::ostringstream text;
			text << in.rdbuf();
			a_text = text.str();
			return static_cast<bool>(in) || in.eof();
		}

		// Written next to the target, then moved over it: a reader never sees half a file, and a Vortex hard link is replaced
		// by a plain file instead of writing through into Vortex's staging copy.
		bool WriteFile(const fs::path& a_path, const std::string& a_text, std::string& a_error)
		{
			std::error_code ec;
			fs::create_directories(a_path.parent_path(), ec);
			auto temp = a_path;
			temp += L".tmp";
			{
				std::ofstream out(temp, std::ios::binary | std::ios::trunc);
				out.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
				if (!out) {
					a_error = "could not write " + temp.string();
					return false;
				}
			}
			if (!MoveFileExW(temp.c_str(), a_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
				a_error = "could not replace " + a_path.string() + " (error " + std::to_string(GetLastError()) + ")";
				fs::remove(temp, ec);
				return false;
			}
			return true;
		}

		// Rewritten in place, like the Python builder's app.set_preset (owner, 2026-11-22): MenusRevamped.ini is a Vortex hard
		// link, and a replaced copy did not survive the Phase 2 run (the link came back with uPreset=1); through the link the
		// change stays.
		bool WriteInPlace(const fs::path& a_path, const std::string& a_text, std::string& a_error)
		{
			std::error_code ec;
			fs::create_directories(a_path.parent_path(), ec);
			std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
			out.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
			out.flush();
			if (!out) {
				a_error = "could not write " + a_path.string();
				return false;
			}
			return true;
		}

		void SetStatus(std::string a_text, bool a_error = false)
		{
			S().status = std::move(a_text);
			S().statusError = a_error;
		}

		// window.refresh_status: what differs from the installed packs (their manifests' style and sizes)
		void RefreshPackStatus()
		{
			auto&       s = S();
			std::string text, error;
			pack::json::Value colour, layout;
			const bool  haveColour = ReadFile(PackManifest(L"MRCustom_P"), text) && pack::json::Parse(text, colour, error);
			const bool  haveLayout = ReadFile(PackManifest(L"MRLayout_P"), text) && pack::json::Parse(text, layout, error);
			const auto  stale = pack::NeedsBuild(s.model.Entries(), haveColour ? colour.Find("style") : nullptr);
			const bool  sizes = !pack::SizesChanged(s.model, haveLayout ? layout.Find("sizes") : nullptr).empty();
			s.packStatus = pack::BuildStatus(s.model.Entries(), stale, sizes);
			const size_t shown = 3, total = stale.size() + (sizes ? 1 : 0);
			if (total <= shown) {
				s.packShort = s.packStatus;
				return;
			}
			std::vector<std::string> first(stale.begin(), stale.begin() + static_cast<std::ptrdiff_t>(std::min(shown, stale.size())));
			s.packShort = pack::BuildStatus(s.model.Entries(), first, sizes && first.size() < shown) + " and " + std::to_string(total - shown) + " more";
		}

		void Changed()
		{
			const auto text = S().model.Render();
			S().dirty = text != S().saved;
			RefreshPackStatus();
			if (S().live.colours) S().live.colours(text);   // Phase 4a: the live preview follows every edit
		}

		void Load()
		{
			auto&       s = S();
			std::string text, error;
			ReadFile(ColourFile(), text);
			if (!s.model.Load(text, error)) {
				s.broken = error;
				s.loaded = true;
				return;
			}
			s.broken.clear();
			s.saved = text;
			s.loaded = true;
			Changed();
		}

		void Save()
		{
			auto&       s = S();
			std::string error;
			const auto  text = s.model.Render();
			if (!WriteFile(ColourFile(), text, error)) {
				SetStatus("Could not write the colour file: " + error, true);
				return;
			}
			s.saved = text;
			s.dirty = false;
			SetStatus("Saved " + ColourFile().string() + ". " + kSaveNote);
		}

		void Vanilla()
		{
			std::string text, error;
			ReadFile(PluginIni(), text);
			if (!WriteInPlace(PluginIni(), model::SetPreset(text, 0), error)) {
				SetStatus("Could not write MenusRevamped.ini (" + error + ").", true);
				return;
			}
			SetStatus("Classic preset set (uPreset=0 in MenusRevamped.ini). Restart the game: the game's own colours show and the sizes stay. "
			          "A pack build switches back to the Custom colours (uPreset=2).");
		}

		void OpenLog()
		{
			std::error_code ec;
			const auto      log = BuilderLog();
			if (!fs::is_regular_file(log, ec)) {
				SetStatus("No build log yet: it is written by the first Build.");
				return;
			}
			std::thread([log] {   // the shell call can take a moment: never on the render thread
				CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
				ShellExecuteW(nullptr, L"open", log.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
				CoUninitialize();
			}).detach();
			SetStatus("Opened " + log.string() + " (outside the game).");
		}

		// ---- widgets ----
		struct Field
		{
			std::string buffer;
			bool        editing = false;
		};

		std::unordered_map<std::string, Field>& Fields()
		{
			static std::unordered_map<std::string, Field> fields;
			return fields;
		}

		// A value field: shows the model's text; a changed value is taken on Enter or when the field loses focus. true + a_typed
		// when there is something to take.
		bool ValueField(const std::string& a_id, const std::string& a_text, float a_width, std::string& a_typed)
		{
			auto& f = Fields()[a_id];
			if (!f.editing) f.buffer = a_text;
			char buffer[128];
			const auto n = std::min(f.buffer.size(), sizeof(buffer) - 1);
			f.buffer.copy(buffer, n);
			buffer[n] = 0;
			ImGui::SetNextItemWidth(a_width);
			ImGui::InputText(("##" + a_id).c_str(), buffer, sizeof(buffer));
			f.buffer = buffer;
			if (ImGui::IsItemActivated()) f.editing = true;
			if (ImGui::IsItemDeactivated()) {
				f.editing = false;
				if (model::Strip(f.buffer) != a_text) {
					a_typed = f.buffer;
					return true;
				}
			}
			return false;
		}

		void Take(bool a_ok, const std::string& a_error)
		{
			if (a_ok) {
				Changed();
				SetStatus(S().dirty ? "Unsaved changes: Save to keep them." : kSaveNote);
			} else {
				SetStatus(a_error, true);
			}
		}

		// A colour swatch; a click opens the picker (sRGB). true + a_rgb (0-255) while the picker changes it.
		bool Swatch(const std::string& a_id, std::array<int, 3> a_current, const char* a_title, std::array<double, 3>& a_rgb)
		{
			float colour[3]{ a_current[0] / 255.0f, a_current[1] / 255.0f, a_current[2] / 255.0f };
			ImGui::PushID(a_id.c_str());
			const bool changed = ImGui::ColorEdit3(a_title, colour,
				ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB |
					ImGuiColorEditFlags_PickerHueBar);
			ImGui::PopID();
			if (changed)
				for (int i = 0; i < 3; ++i) a_rgb[i] = static_cast<double>(colour[i]) * 255.0;
			return changed;
		}

		void OwnAndReset(bool a_own, const std::string& a_id, bool& a_reset)
		{
			if (a_own) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextColored(kAmber, "own value");
				ImGui::SameLine();
				ImGui::PushID(a_id.c_str());
				a_reset = ImGui::SmallButton("Reset");
				ImGui::PopID();
			}
		}

		bool BeginRows(const char* a_id, float a_fieldWidth, bool a_own)
		{
			if (!ImGui::BeginTable(a_id, 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) return false;
			const float em = ImGui::GetFontSize();
			ImGui::TableSetupColumn("swatch", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
			ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthFixed, a_fieldWidth);
			ImGui::TableSetupColumn("own", ImGuiTableColumnFlags_WidthFixed, a_own ? em * 7.5f : 1.0f);
			return true;
		}

		void Label(const std::string& a_text, bool a_member)
		{
			ImGui::AlignTextToFramePadding();
			if (a_member) {
				ImGui::Indent(ImGui::GetFontSize());
				ImGui::PushStyleColor(ImGuiCol_Text, kInkDim);
				ImGui::TextWrapped("%s", a_text.c_str());
				ImGui::PopStyleColor();
				ImGui::Unindent(ImGui::GetFontSize());
			} else {
				ImGui::TextWrapped("%s", a_text.c_str());
			}
		}

		// A colour element row (Colours Advanced, Runtime): swatch, what and where, value, own value + Reset.
		void ElementRow(const std::string& a_key, bool a_member)
		{
			auto&       m = S().model;
			const auto& e = m.ByKey(a_key);
			std::string error, typed;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			std::array<double, 3> rgb{};
			if (Swatch(a_key, m.Swatch(a_key), e.help.c_str(), rgb)) {
				m.SetRgb(a_key, rgb);
				Take(true, {});
			}
			ImGui::TableNextColumn();
			Label(e.help, a_member);
			ImGui::TableNextColumn();
			if (ValueField(a_key, m.Text(a_key), -FLT_MIN, typed)) Take(m.SetText(a_key, typed, error), error);
			ImGui::TableNextColumn();
			bool reset = false;
			OwnAndReset(a_member && m.IsOwn(a_key), a_key, reset);
			if (reset) {
				m.ResetElement(a_key);
				Take(true, {});
			}
		}

		void ColoursTab()
		{
			auto&       m = S().model;
			const float em = ImGui::GetFontSize();
			if (!BeginRows("roles", em * 8.5f, S().advanced)) return;
			for (const auto& role : model::Roles()) {
				std::string error, typed;
				ImGui::TableNextRow();
				if (S().advanced) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImVec4(0.12f, 0.09f, 0.05f, 0.55f)));
				ImGui::TableNextColumn();
				std::array<double, 3> rgb{};
				if (Swatch(std::string("role.") + role.key, m.RoleSwatch(role.key), role.label, rgb)) Take(m.SetRole(role.key, rgb, error), error);
				ImGui::TableNextColumn();
				Label(role.label, false);
				ImGui::TableNextColumn();
				if (ValueField(std::string("role.") + role.key, m.RoleText(role.key), -FLT_MIN, typed)) Take(m.SetRoleText(role.key, typed, error), error);
				ImGui::TableNextColumn();
				if (S().advanced)
					for (const auto& member : role.members) ElementRow(member, true);
			}
			ImGui::EndTable();
		}

		void SizeRow(const std::string& a_key, const std::string& a_label, bool a_member)
		{
			auto&       m = S().model;
			std::string error, typed;
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TableNextColumn();
			Label(a_label, a_member);
			ImGui::TableNextColumn();
			if (ValueField(a_key, m.SizeText(a_key), -FLT_MIN, typed)) Take(m.SetSize(a_key, typed, error), error);
			ImGui::TableNextColumn();
			bool reset = false;
			OwnAndReset(a_member && m.IsOwnSize(a_key), a_key, reset);
			if (reset) {
				m.ResetSize(a_key);
				Take(true, {});
			}
		}

		void TextTab()
		{
			const float em = ImGui::GetFontSize();
			ImGui::TextColored(kInkDim, "Sizes in percent of today's size; Advanced shows each text's own point size.");
			for (const auto& role : model::TextRoles()) {
				ImGui::SeparatorText(role.label);
				if (!BeginRows(role.key, em * 5.0f, S().advanced)) continue;
				SizeRow(role.key, "Size (%)", false);
				if (S().advanced)
					for (const auto& member : role.members) {
						std::string label = member;
						std::replace(label.begin(), label.end(), '.', ':');
						label.insert(label.find(':') + 1, " ");
						SizeRow("font." + member, label, true);
					}
				ImGui::EndTable();
			}
		}

		void RowsTab()
		{
			const float em = ImGui::GetFontSize();
			for (const auto& list : model::RowLists()) {
				ImGui::SeparatorText(list.label);
				if (!BeginRows(list.label, em * 5.0f, false)) continue;
				for (const auto& row : list.rows) SizeRow(row.key, row.what, false);
				ImGui::EndTable();
			}
		}

		void RuntimeTab()
		{
			const float em = ImGui::GetFontSize();
			ImGui::TextColored(kInkDim, "Our own widgets: linear red, green, blue, alpha (0-1); applied at the next game start.");
			for (const auto& group : model::RuntimeGroups()) {
				ImGui::SeparatorText(group.heading);
				if (!BeginRows(group.heading, em * 14.0f, false)) continue;
				for (const auto& key : group.keys) ElementRow(key, false);
				ImGui::EndTable();
			}
		}

		bool TabItem(const char* a_label, int a_index)
		{
			const int  preview = S().previewTab;
			const auto flags = preview == a_index ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			return ImGui::BeginTabItem(a_label, nullptr, flags);
		}

		void Page(void (*a_draw)())
		{
			if (ImGui::BeginChild("page", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) a_draw();
			ImGui::EndChild();
		}

		bool RightButton(const char* a_label)
		{
			const float width = ImGui::CalcTextSize(a_label).x + ImGui::GetStyle().FramePadding.x * 4;
			ImGui::SameLine(ImGui::GetContentRegionMax().x - width);
			return ImGui::Button(a_label, ImVec2(width, 0));
		}

		bool BrokenFile()
		{
			auto& s = S();
			ImGui::TextColored(kError, "The colour file cannot be read:");
			ImGui::TextWrapped("%s", s.broken.c_str());
			ImGui::Spacing();
			ImGui::TextWrapped("Start from the Dark colours? The file is kept as CustomColours.ini.broken and rewritten at the next Save.");
			ImGui::Spacing();
			if (ImGui::Button("Start from the Dark colours")) {
				auto backup = ColourFile();
				backup += L".broken";
				MoveFileExW(ColourFile().c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING);
				Load();
				SetStatus(s.broken.empty() ? std::string("Started from the Dark colours (not saved yet): Save to keep them.") : s.broken, !s.broken.empty());
			}
			return !RightButton("Close");
		}
	}

	void SetLiveHooks(LiveHooks a_hooks) { S().live = std::move(a_hooks); }

	void SetPluginFolder(const std::filesystem::path& a_folder) { S().plugins = a_folder; }

	void SelectForPreview(int a_tab, bool a_advanced)
	{
		S().previewTab = a_tab;
		S().advanced = a_advanced;
	}

	void LoadFont()
	{
		auto& io = ImGui::GetIO();
		wchar_t windows[MAX_PATH]{};
		GetWindowsDirectoryW(windows, MAX_PATH);
		std::error_code ec;
		const auto font = std::filesystem::path(windows) / L"Fonts" / L"georgia.ttf";
		if (std::filesystem::is_regular_file(font, ec) && io.Fonts->AddFontFromFileTTF(font.string().c_str(), 18.0f)) return;
		io.Fonts->AddFontDefault();
	}

	void ApplyStyle(float a_viewportHeight)
	{
		const float scale = std::clamp(a_viewportHeight / 1080.0f, 0.75f, 3.0f);
		ImGuiStyle style;
		ImGui::StyleColorsDark(&style);

		auto Rgb = [](float a_r, float a_g, float a_b, float a_a = 1.0f) { return ImVec4(a_r, a_g, a_b, a_a); };
		const ImVec4 paper = Rgb(0.045f, 0.040f, 0.034f, 0.97f);
		const ImVec4 paperDeep = Rgb(0.025f, 0.022f, 0.018f, 1.0f);
		const ImVec4 bronze = Rgb(0.38f, 0.29f, 0.17f);
		const ImVec4 bronzeDim = Rgb(0.24f, 0.18f, 0.11f);
		const ImVec4 amber = kAmber;
		const ImVec4 amberDark = Rgb(0.45f, 0.31f, 0.12f);

		auto* c = style.Colors;
		c[ImGuiCol_Text] = kInk;
		c[ImGuiCol_TextDisabled] = kInkDim;
		c[ImGuiCol_WindowBg] = paper;
		c[ImGuiCol_ChildBg] = paperDeep;
		c[ImGuiCol_PopupBg] = paper;
		c[ImGuiCol_Border] = bronze;
		c[ImGuiCol_BorderShadow] = Rgb(0, 0, 0, 0);
		c[ImGuiCol_FrameBg] = paperDeep;
		c[ImGuiCol_FrameBgHovered] = Rgb(0.12f, 0.09f, 0.05f);
		c[ImGuiCol_FrameBgActive] = Rgb(0.18f, 0.13f, 0.07f);
		c[ImGuiCol_TitleBg] = paperDeep;
		c[ImGuiCol_TitleBgActive] = paperDeep;
		c[ImGuiCol_TitleBgCollapsed] = paperDeep;
		c[ImGuiCol_ScrollbarBg] = paperDeep;
		c[ImGuiCol_ScrollbarGrab] = bronzeDim;
		c[ImGuiCol_ScrollbarGrabHovered] = bronze;
		c[ImGuiCol_ScrollbarGrabActive] = amber;
		c[ImGuiCol_CheckMark] = amber;
		c[ImGuiCol_SliderGrab] = bronze;
		c[ImGuiCol_SliderGrabActive] = amber;
		c[ImGuiCol_Button] = Rgb(0.10f, 0.08f, 0.05f);
		c[ImGuiCol_ButtonHovered] = amberDark;
		c[ImGuiCol_ButtonActive] = amber;
		c[ImGuiCol_Header] = Rgb(0.16f, 0.12f, 0.07f);
		c[ImGuiCol_HeaderHovered] = amberDark;
		c[ImGuiCol_HeaderActive] = amber;
		c[ImGuiCol_Separator] = bronzeDim;
		c[ImGuiCol_SeparatorHovered] = bronze;
		c[ImGuiCol_SeparatorActive] = amber;
		c[ImGuiCol_ResizeGrip] = bronzeDim;
		c[ImGuiCol_ResizeGripHovered] = bronze;
		c[ImGuiCol_ResizeGripActive] = amber;
		c[ImGuiCol_Tab] = Rgb(0.08f, 0.065f, 0.045f);
		c[ImGuiCol_TabHovered] = amberDark;
		c[ImGuiCol_TabSelected] = Rgb(0.20f, 0.15f, 0.08f);
		c[ImGuiCol_TabSelectedOverline] = amber;
		c[ImGuiCol_TabDimmed] = c[ImGuiCol_Tab];
		c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
		c[ImGuiCol_TabDimmedSelectedOverline] = bronze;
		c[ImGuiCol_TableRowBg] = Rgb(0, 0, 0, 0);
		c[ImGuiCol_TableRowBgAlt] = Rgb(0, 0, 0, 0);
		c[ImGuiCol_TextSelectedBg] = Rgb(0.82f, 0.58f, 0.22f, 0.35f);
		c[ImGuiCol_NavCursor] = amber;
		c[ImGuiCol_ModalWindowDimBg] = Rgb(0, 0, 0, 0.5f);

		style.WindowRounding = 2.0f;
		style.FrameRounding = 2.0f;
		style.TabRounding = 2.0f;
		style.GrabRounding = 2.0f;
		style.WindowBorderSize = 1.0f;
		style.FrameBorderSize = 1.0f;
		style.TabBorderSize = 1.0f;
		style.WindowPadding = ImVec2(16, 14);
		style.FramePadding = ImVec2(10, 6);
		style.ItemSpacing = ImVec2(10, 8);
		style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
		style.SeparatorTextBorderSize = 2.0f;
		style.ScaleAllSizes(scale);
		style.FontSizeBase = std::round(22.0f * scale);
		ImGui::GetStyle() = style;
	}

	bool Draw()
	{
		auto&       s = S();
		const auto* viewport = ImGui::GetMainViewport();
		const ImVec2 size(viewport->Size.x * 0.50f, viewport->Size.y * 0.72f);
		ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f), ImGuiCond_Appearing,
			ImVec2(0.5f, 0.5f));
		bool open = true;
		const char* title = s.dirty ? "Menus Revamped - Pack Builder *###builder" : "Menus Revamped - Pack Builder###builder";
		if (!ImGui::Begin(title, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::End();
			return open;
		}
		if (!s.loaded || (ImGui::IsWindowAppearing() && !s.dirty)) Load();   // re-read at each opening unless edits are pending
		if (!s.broken.empty()) {
			open = BrokenFile() && open;
			ImGui::End();
			return open;
		}

		ImGui::Checkbox("Advanced (each element)", &s.advanced);
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("  Colour file: MenusRevamped\\CustomColours.ini");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ColourFile().string().c_str());

		const auto& style = ImGui::GetStyle();
		const auto  live = s.live.status ? s.live.status() : std::string();   // Phase 4a: "Live preview: ..." while it prepares or when it is off
		const float footer = ImGui::GetFrameHeight() + ImGui::GetTextLineHeightWithSpacing() * (live.empty() ? 3 : 4) + style.ItemSpacing.y * (live.empty() ? 5 : 6);
		if (ImGui::BeginChild("tabs", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
			if (ImGui::BeginTabBar("builder")) {
				if (TabItem("Colours", 0)) {
					Page(ColoursTab);
					ImGui::EndTabItem();
				}
				if (TabItem("Text", 1)) {
					Page(TextTab);
					ImGui::EndTabItem();
				}
				if (TabItem("Rows", 2)) {
					Page(RowsTab);
					ImGui::EndTabItem();
				}
				if (TabItem("Runtime", 3)) {
					Page(RuntimeTab);
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();
		ImGui::Separator();
		const auto build = pack::job::Get();
		if (build.state == pack::job::State::Running) {
			s.buildReported = false;
			const float fraction = build.total > 0 ? static_cast<float>(build.done) / static_cast<float>(build.total) : 0.0f;
			const std::string line = build.total > 0 ? build.phase + " (" + std::to_string(build.done) + " of " + std::to_string(build.total) + ")" : build.phase;
			ImGui::ProgressBar(fraction, ImVec2(-1, 0), line.c_str());
		} else {
			if (!s.buildReported && build.state != pack::job::State::Idle) {
				s.buildReported = true;
				SetStatus(build.message, build.state == pack::job::State::Failed);
				RefreshPackStatus();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, s.statusError ? kError : kInkDim);
			ImGui::TextWrapped("%s", s.status.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, s.packStatus.starts_with("Build pack") ? kAmber : kInkDim);
		ImGui::TextWrapped("%s", s.packShort.c_str());
		ImGui::PopStyleColor();
		if (s.packShort != s.packStatus && ImGui::IsItemHovered()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
			ImGui::SetTooltip("%s", s.packStatus.c_str());
			ImGui::PopTextWrapPos();
		}
		if (!live.empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, s.live.statusError && s.live.statusError() ? kError : kInkDim);
			ImGui::TextWrapped("%s", live.c_str());
			ImGui::PopStyleColor();
		}

		ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeight() - style.WindowPadding.y);
		if (build.state == pack::job::State::Running) {
			if (ImGui::Button("Cancel build")) pack::job::Cancel();
		} else if (ImGui::Button("Build")) {   // window.build: the colour file is saved first (the plugin compares the pack with it)
			Save();
			std::string why;
			if (!s.dirty && !pack::job::StartBuild(s.plugins, why)) SetStatus(why, true);
		}
		if (ImGui::IsItemHovered() && build.state != pack::job::State::Running)
			ImGui::SetTooltip("Saves, then bakes the colours and sizes into the packs (about 30 seconds to a few minutes, in the background).\n"
			                  "The new look shows after a game restart.");
		ImGui::SameLine();
		if (ImGui::Button("Save")) Save();
		ImGui::SameLine();
		if (ImGui::Button("Reset to Dark")) {
			s.model.ResetToDark();
			Changed();
			SetStatus("Reset to the Dark colours and today's sizes (not saved yet): Save to keep them.");
		}
		ImGui::SameLine();
		if (ImGui::Button("Vanilla colours")) ImGui::OpenPopup("Vanilla colours");
		ImGui::SameLine();
		if (ImGui::Button("Open log")) OpenLog();

		if (RightButton("Close")) open = false;

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(size.x * 0.7f, 0), ImGuiCond_Appearing);
		if (ImGui::BeginPopupModal("Vanilla colours", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
			ImGui::TextWrapped("Switch to the game's own colours (uPreset=0 in MenusRevamped.ini)? The sizes stay. "
			                   "It applies at the next game start; a pack build switches back to the Custom colours.");
			ImGui::Spacing();
			if (ImGui::Button("Switch")) {
				Vanilla();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::End();
		return open;
	}
}
