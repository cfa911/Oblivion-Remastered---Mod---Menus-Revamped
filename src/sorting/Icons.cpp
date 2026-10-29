#include "sorting/Icons.h"
#include "engine/Reflect.h"
#include "engine/Ui.h"
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
		HMODULE module = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&Texture), &module);
		wchar_t location[32768]{};
		if (!module || !GetModuleFileNameW(module, location, 32768)) return nullptr;
		const auto path = std::filesystem::path(location).parent_path() / L"MenusRevamped" / L"Sorting" / filename;
		UE::UObject* texture = nullptr;
		if (std::filesystem::is_regular_file(path)) {
			auto* library = ui::Class(L"/Script/Engine.KismetRenderingLibrary");
			ui::Call import(library ? library->GetDefaultObject(false) : nullptr, L"ImportFileAsTexture2D");
			if (void* field = import.At("Filename")) {
				import.Set("WorldContextObject", ui::PlayerController());
				auto* value = new (field) UE::FString(path.wstring().c_str());
				texture = import.RunGuarded() ? import.Get<UE::UObject*>("ReturnValue") : nullptr;
				value->~FString();
			}
		}
		loaded.emplace(filename, texture ? reflect::Hold(texture) : reflect::Handle{});
		if (!texture) logger::warn("Sorting Overhaul: missing or unreadable icon {} (restart the game after replacing PNGs)", path.filename().string());
		return texture;
	}
}
