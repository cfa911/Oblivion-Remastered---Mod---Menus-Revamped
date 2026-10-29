#include "engine/Ui.h"

#include "engine/PEHook.h"
#include "engine/Reflect.h"

// Calling engine functions through reflection, class and asset lookups, the player controller.
namespace ui
{
	namespace
	{
		constexpr std::ptrdiff_t kFieldNext = 0x18;
		constexpr std::ptrdiff_t kFieldName = 0x20;
		constexpr std::ptrdiff_t kElementSize = 0x34;

		std::string Narrow(const UE::FName& a_n)
		{
			const auto s = a_n.ToString();
			const wchar_t* d = UE::GetData(s);
			std::string out;
			for (int i = 0, n = UE::GetNum(s); d && i < n && d[i]; ++i) out.push_back(d[i] < 0x80 ? static_cast<char>(d[i]) : '?');
			return out;
		}

		bool ProcessEventGuarded(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
		{
			__try {
				a_obj->ProcessEvent(a_fn, a_params);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}
	}

	std::int32_t SizeOf(UE::UStruct* a_struct, std::string_view a_name)
	{
		for (UE::UStruct* s = a_struct; s; s = s->superStruct) {
			for (auto* f = reinterpret_cast<std::uint8_t*>(s->childProperties); f; f = *reinterpret_cast<std::uint8_t**>(f + kFieldNext)) {
				if (Narrow(*reinterpret_cast<const UE::FName*>(f + kFieldName)) == a_name) {
					return *reinterpret_cast<const std::int32_t*>(f + kElementSize);
				}
			}
		}
		return -1;
	}

	Call::Call(UE::UObject* a_obj, const wchar_t* a_fn) :
		m_obj(a_obj),
		m_fn(a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr)
	{
		if (m_fn) m_params.assign(static_cast<std::size_t>(std::max(reinterpret_cast<UE::UStruct*>(m_fn)->propertiesSize, 0)) + 16, 0);
	}

	void* Call::At(std::string_view a_name)
	{
		if (!m_fn) return nullptr;
		const auto off = reflect::Offset(reinterpret_cast<UE::UStruct*>(m_fn), a_name);
		return off >= 0 ? m_params.data() + off : nullptr;
	}

	bool Call::Run()
	{
		if (!m_fn || !m_obj) return false;
		pe::OwnCall own(m_obj, m_fn);
		m_obj->ProcessEvent(m_fn, m_params.data());
		return true;
	}

	bool GuardedProcessEvent(UE::UObject* a_obj, UE::UFunction* a_fn, void* a_params)
	{
		pe::OwnCall own(a_obj, a_fn);   // __try cannot share a function with a destructor: the guard is its own function
		return ProcessEventGuarded(a_obj, a_fn, a_params);
	}

	bool Call::RunGuarded() { return m_fn && m_obj && GuardedProcessEvent(m_obj, m_fn, m_params.data()); }

	bool CallFirst(UE::UObject* a_obj, const wchar_t* a_fn, const void* a_bytes, std::size_t a_size)
	{
		auto* fn = a_obj ? a_obj->FindFunction(UE::FName(a_fn, UE::EFindName::Find)) : nullptr;
		auto* st = reinterpret_cast<UE::UStruct*>(fn);
		auto* f = st ? reinterpret_cast<std::uint8_t*>(st->childProperties) : nullptr;
		const auto off = f ? *reinterpret_cast<const std::int32_t*>(f + 0x44) : -1;
		if (off < 0) return false;
		std::vector<std::uint8_t> params(static_cast<std::size_t>(std::max(st->propertiesSize, 0)) + a_size + 16, 0);
		std::memcpy(params.data() + off, a_bytes, a_size);
		pe::OwnCall own(a_obj, fn);
		a_obj->ProcessEvent(fn, params.data());
		return true;
	}

	void Vec2(UE::UObject* a_obj, const wchar_t* a_fn, double a_x, double a_y)
	{
		const double v[2] = { a_x, a_y };
		CallFirst(a_obj, a_fn, v, sizeof(v));
	}

	void Float(UE::UObject* a_obj, const wchar_t* a_fn, float a_v) { CallFirst(a_obj, a_fn, &a_v, sizeof(a_v)); }

	void Visible(UE::UObject* a_widget, bool a_visible)
	{
		if (!a_widget) return;
		Call c(a_widget, L"SetVisibility");
		c.Set("InVisibility", static_cast<std::uint8_t>(a_visible ? 3 : 1));
		c.Run();
	}

	void Colour(UE::UObject* a_image, float a_r, float a_g, float a_b, float a_a)
	{
		const float c[4] = { a_r, a_g, a_b, a_a };
		CallFirst(a_image, L"SetColorAndOpacity", c, sizeof(c));
	}

	UE::UClass* Class(const wchar_t* a_path) { return UE::StaticFindObject<UE::UClass>(nullptr, nullptr, a_path); }

	UE::UObject* Load(const wchar_t* a_path)
	{
		if (!a_path) return nullptr;
		if (auto* o = UE::StaticFindObject<UE::UObject>(nullptr, nullptr, a_path)) return o;
		static auto* lib = Class(L"/Script/Engine.KismetSystemLibrary");
		auto*        cdo = lib ? lib->GetDefaultObject(false) : nullptr;
		if (!cdo) return nullptr;
		Call mk(cdo, L"MakeSoftObjectPath");
		Call conv(cdo, L"Conv_SoftObjPathToSoftObjRef");
		Call load(cdo, L"LoadAsset_Blocking");
		void*      path = mk.At("PathString");
		const auto pathSize = mk.Size("ReturnValue");
		const auto refSize = conv.Size("ReturnValue");
		if (!mk || !conv || !load || !path || pathSize <= 0 || refSize <= 0 || conv.Size("SoftObjectPath") != pathSize || load.Size("Asset") != refSize) {
			return nullptr;
		}
		// the FString is built in place and never destroyed - a small, one-time leak per asset (as in Minimap Menu)
		new (path) UE::FString(a_path);
		mk.Run();
		std::memcpy(conv.At("SoftObjectPath"), mk.At("ReturnValue"), static_cast<std::size_t>(pathSize));
		conv.Run();
		std::memcpy(load.At("Asset"), conv.At("ReturnValue"), static_cast<std::size_t>(refSize));
		load.Run();
		return load.Get<UE::UObject*>("ReturnValue");
	}

	double RealTimeSeconds()
	{
		static auto* statics = Class(L"/Script/Engine.GameplayStatics");
		auto*        cdo = statics ? statics->GetDefaultObject(false) : nullptr;
		auto*        world = PlayerController();
		if (!cdo || !world) return -1.0;
		Call c(cdo, L"GetRealTimeSeconds");
		if (!c || !c.Set("WorldContextObject", world) || !c.RunGuarded()) return -1.0;
		return c.Size("ReturnValue") == 8 ? c.Get<double>("ReturnValue") : static_cast<double>(c.Get<float>("ReturnValue"));
	}

	UE::UObject* PlayerController()
	{
		static reflect::Handle cached;
		static ULONGLONG       lastScan = 0;
		if (auto* pc = reflect::Get(cached)) return pc;
		const ULONGLONG now = GetTickCount64();
		if (now - lastScan < 2000) return nullptr;
		lastScan = now;
		// the game's controller is a SUBCLASS (VAltarPlayerController): reflect::Instances matches one class exactly and so
		// never found it - the ammo wheel was never built ("cannot be built yet", every press, 2026-09-29 23:55 on)
		auto* cls = Class(L"/Script/Engine.PlayerController");
		auto* arr = UE::FUObjectArray::GetSingleton();
		UE::UObject* found = nullptr;
		if (cls && arr) {
			arr->LockInternalArray();
			const std::int32_t n = arr->GetObjectArrayNum();
			for (std::int32_t i = 0; i < n && !found; ++i) {
				auto* item = arr->IndexToObject(i);
				auto* o = item ? reinterpret_cast<UE::UObject*>(item->object) : nullptr;
				auto* oc = o ? o->GetClass() : nullptr;
				// not a class default object, not an archetype
				if (oc && oc->IsChildOf(cls) && (static_cast<std::int32_t>(o->objectFlags) & 0x30) == 0) {
					found = o;
				}
			}
			arr->UnlockInternalArray();
		}
		cached = found ? reflect::Hold(found) : reflect::Handle{};
		return found;
	}
}
