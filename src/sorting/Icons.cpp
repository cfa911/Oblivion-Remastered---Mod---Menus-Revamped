#include "sorting/Icons.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
#include "core/Config.h"
#include <unordered_map>
namespace icons
{
	UE::UObject* Texture(const wchar_t* filename)
	{
		static std::unordered_map<std::wstring, reflect::Handle> loaded;
		auto it = loaded.find(filename);
		if (it != loaded.end()) {
			if (auto* texture = reflect::Get(it->second)) return texture;
			if (!it->second.ptr) return nullptr; // A missing file is reported only once per run.
			loaded.erase(it); // A previous menu and its textures may have been collected.
		}
		const auto folder = mrconfig::PluginFolder();
		if (folder.empty()) return nullptr;
		const auto path = folder / L"MenusRevamped" / L"Sorting" / filename;
		auto* texture = ui::ImportTexture(path);
		loaded.emplace(filename, texture ? reflect::Hold(texture) : reflect::Handle{});
		if (!texture) logger::warn("Menus Revamped: missing or unreadable icon {} (restart the game after replacing PNGs)", path.filename().string());
		return texture;
	}
}
