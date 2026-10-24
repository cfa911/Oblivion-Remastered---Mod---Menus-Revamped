#include "GearData.h"

#include "ItemTags.h"

#include <memory>

namespace geardata
{
	namespace
	{
		using Idle = void (*)(void*);
		Idle g_original = nullptr;
		std::mutex g_lock;
		std::atomic<std::uint64_t> g_epoch{ 0 };
		std::atomic<bool> g_pending{ false };
		struct Work
		{
			std::uint64_t ticket = 0, deadline = 0;
			RE::PlayerCharacter* player = nullptr;
			std::vector<int> indices;
			std::vector<Entry> entries;
		};
		std::unique_ptr<Work> g_queued, g_ready;  // lock protected
		std::unique_ptr<Work> g_work;             // TES callback only

		bool Read(const void* a_source, void* a_target, std::size_t a_size)
		{
			__try { std::memcpy(a_target, a_source, a_size); return true; }
			__except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
		}

		bool TesThread()
		{
			static REL::Relocation<std::uintptr_t> singleton{ REL::ID(1667596) };
			std::uintptr_t main = 0;
			DWORD id = 0;
			return Read(reinterpret_cast<void*>(singleton.address()), &main, sizeof(main)) && main &&
			       Read(reinterpret_cast<void*>(main + 0x18), &id, sizeof(id)) && id == GetCurrentThreadId();
		}

		// Every native item kind gets the tag bits of the choice(s) it belongs to on its own tab. An item with no tag is
		// valid but belongs to no specific choice (for example an ingredient on the potions tab).
		bool SubTags(RE::ItemChange* a_item, std::uint32_t& a_tags, unsigned& a_type)
		{
			__try {
				auto* object = a_item ? a_item->object : nullptr;
				if (!object) return false;
				a_tags = 0;
				a_type = static_cast<unsigned>(object->GetFormType());
				if (auto* armor = object->As<RE::TESObjectARMO>()) { a_tags = itemtags::GearTags(armor->bipedModelData.bipedObjectSlots); return true; }
				if (auto* clothing = object->As<RE::TESObjectCLOT>()) { a_tags = itemtags::GearTags(clothing->bipedModelData.bipedObjectSlots); return true; }
				if (auto* weapon = object->As<RE::TESObjectWEAP>()) { a_tags = itemtags::WeaponTags(weapon->data.type); return true; }
				if (auto* potion = object->As<RE::AlchemyItem>()) {
					std::uint32_t codes[8]{};
					unsigned count = 0;
					auto** node = reinterpret_cast<void**>(&static_cast<RE::EffectItemList*>(potion)->effectList);
					for (int n = 0; node && n < 8; ++n) {
						auto* effect = reinterpret_cast<std::uint8_t*>(node[0]);
						if (effect) codes[count++] = *reinterpret_cast<std::uint32_t*>(effect + 0x08);
						node = reinterpret_cast<void**>(node[1]);
					}
					a_tags = itemtags::PotionTags(codes, count, static_cast<RE::EffectItemList*>(potion)->hostileCount);
					return true;
				}
				a_tags = itemtags::MiscTags(static_cast<unsigned>(object->GetFormType()), object->GetFormID(), 0);
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
		}

		bool Tags(RE::ItemChange* a_item, std::uint32_t& a_tags)
		{
			unsigned type = 0;
			if (!SubTags(a_item, a_tags, type)) return false;
			a_tags = (a_tags & itemtags::kTagBits) | (itemtags::Category(type) << 28);
			return true;
		}

		void AfterIdle(void* a_menu)
		{
			g_original(a_menu);
			if (!g_pending.load(std::memory_order_acquire) || !TesThread()) return;
			if (g_work && g_work->ticket != g_epoch.load()) g_work.reset();
			if (!g_work) {
				std::scoped_lock lock(g_lock);
				g_work = std::move(g_queued);
				if (!g_work) return;
			}
			auto& work = *g_work;
			auto* player = RE::PlayerCharacter::GetSingleton();
			const bool context = player && player == work.player && GetTickCount64() <= work.deadline;
			static REL::Relocation<RE::ItemChange* (*)(RE::TESObjectREFR*, int, bool)> resolve{ REL::ID(405455) };
			static REL::Relocation<void (*)(RE::ItemChange*)> destroy{ REL::ID(407566) };
			static REL::Relocation<void (*)(void*, std::size_t)> deallocate{ REL::ID(106342) };
			const auto end = (std::min)(work.entries.size() + 32, work.indices.size());
			while (work.entries.size() < end) {
				const int index = work.indices[work.entries.size()];
				Entry entry{ index, 0, false };
				auto* copy = context ? resolve(player, index, false) : nullptr;
				entry.valid = Tags(copy, entry.tags);
				if (copy) { destroy(copy); deallocate(copy, sizeof(RE::ItemChange)); }
				work.entries.push_back(entry);
			}
			if (work.entries.size() == work.indices.size()) {
				std::scoped_lock lock(g_lock);
				if (work.ticket == g_epoch.load()) {
					g_ready = std::move(g_work);
					g_pending.store(false, std::memory_order_release);
				} else g_work.reset();
			}
		}

		bool Install()
		{
			if (g_original) return true;
			static REL::Relocation<std::uintptr_t> vtable{ REL::ID(1043093) };
			static REL::Relocation<std::uintptr_t> idle{ REL::ID(410647) };
			auto* slot = reinterpret_cast<Idle*>(vtable.address() + 0x12 * sizeof(void*));
			if (reinterpret_cast<std::uintptr_t>(*slot) != idle.address()) return false;
			DWORD previous = 0;
			if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) return false;
			g_original = *slot;
			InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot), reinterpret_cast<void*>(&AfterIdle));
			DWORD ignored = 0;
			VirtualProtect(slot, sizeof(void*), previous, &ignored);
			return true;
		}
	}

	std::uint64_t Request(std::vector<int> a_indices)
	{
		if (a_indices.empty() || a_indices.size() > 4000 ||
			std::any_of(a_indices.begin(), a_indices.end(), [](int i) { return i < 0 || i >= 1000000; }) || !Install()) return 0;
		std::scoped_lock lock(g_lock);
		auto work = std::make_unique<Work>();
		work->ticket = ++g_epoch;
		work->deadline = GetTickCount64() + 2000;
		work->player = RE::PlayerCharacter::GetSingleton();
		work->indices = std::move(a_indices);
		const auto ticket = work->ticket;
		g_queued = std::move(work);
		g_ready.reset();
		g_pending.store(true, std::memory_order_release);
		return ticket;
	}

	bool Take(std::uint64_t a_ticket, std::vector<Entry>& a_entries)
	{
		std::scoped_lock lock(g_lock);
		if (!g_ready || g_ready->ticket != a_ticket) return false;
		a_entries = std::move(g_ready->entries);
		g_ready.reset();
		return true;
	}

	void Cancel()
	{
		std::scoped_lock lock(g_lock);
		++g_epoch;
		g_queued.reset();
		g_ready.reset();
		g_pending.store(false, std::memory_order_release);
	}
}
