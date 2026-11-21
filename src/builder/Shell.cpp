// In-game Pack Builder window contents, Phase 1 shell (KB mods/menus-revamped/evidence/in-game-pack-builder-plan.md).
// SPDX-License-Identifier: GPL-3.0-or-later
// Colours follow the Dark preset of Menus Revamped (core/UiTheme.h kDark: near-black warm paper, bronze border, parchment
// ink, amber focus). No game state is read or written here.
#include "builder/Shell.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>

namespace builder::shell
{
	namespace
	{
		char g_testText[128] = "";

		ImVec4 Rgb(float a_r, float a_g, float a_b, float a_a = 1.0f) { return ImVec4(a_r, a_g, a_b, a_a); }

		void Placeholder(const char* a_what)
		{
			ImGui::Spacing();
			ImGui::TextDisabled("Coming in the next phase:");
			ImGui::TextWrapped("%s", a_what);
		}
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

		const ImVec4 paper = Rgb(0.045f, 0.040f, 0.034f, 0.97f);
		const ImVec4 paperDeep = Rgb(0.025f, 0.022f, 0.018f, 1.0f);
		const ImVec4 bronze = Rgb(0.38f, 0.29f, 0.17f);
		const ImVec4 bronzeDim = Rgb(0.24f, 0.18f, 0.11f);
		const ImVec4 ink = Rgb(0.94f, 0.89f, 0.77f);
		const ImVec4 inkDim = Rgb(0.60f, 0.54f, 0.43f);
		const ImVec4 amber = Rgb(0.82f, 0.58f, 0.22f);
		const ImVec4 amberDark = Rgb(0.45f, 0.31f, 0.12f);

		auto* c = style.Colors;
		c[ImGuiCol_Text] = ink;
		c[ImGuiCol_TextDisabled] = inkDim;
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
		style.ScaleAllSizes(scale);
		style.FontSizeBase = std::round(22.0f * scale);
		ImGui::GetStyle() = style;
	}

	bool Draw()
	{
		const auto* viewport = ImGui::GetMainViewport();
		const ImVec2 size(viewport->Size.x * 0.42f, viewport->Size.y * 0.58f);
		ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f), ImGuiCond_Appearing,
			ImVec2(0.5f, 0.5f));
		bool open = true;
		if (!ImGui::Begin("Menus Revamped - Pack Builder", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
			ImGui::End();
			return open;
		}
		const float footer = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 3;
		if (ImGui::BeginChild("tabs", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
			if (ImGui::BeginTabBar("builder")) {
				if (ImGui::BeginTabItem("Colours")) {
					Placeholder("The Custom preset's colour roles, with Advanced for each texture group.");
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Text")) {
					Placeholder("Text colours and sizes.");
					ImGui::Spacing();
					ImGui::Separator();
					ImGui::Spacing();
					ImGui::TextUnformatted("Text entry test (not saved):");
					ImGui::SetNextItemWidth(-1);
					ImGui::InputText("##test", g_testText, sizeof(g_testText));
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Rows")) {
					Placeholder("Row highlight, separators and list spacing.");
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Runtime")) {
					Placeholder("Options applied while the game runs, without rebuilding the pack.");
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();
		ImGui::Separator();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("Close: F11, or hold LS + RS");
		const char* label = "Close";
		const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 4;
		ImGui::SameLine(ImGui::GetContentRegionMax().x - width);
		if (ImGui::Button(label, ImVec2(width, 0))) open = false;
		ImGui::End();
		return open;
	}
}
