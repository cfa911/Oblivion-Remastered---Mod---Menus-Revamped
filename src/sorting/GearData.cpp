#include "sorting/GearData.h"

#include "sorting/rules/ItemTags.h"

#include <memory>

namespace geardata
{
	namespace
	{
		using Idle = void (*)(void*);
		Idle g_original = nullptr;
		Idle g_originalContainer = nullptr;
		std::mutex g_lock;
		std::atomic<std::uint64_t> g_epoch[3]{};
		std::atomic<bool> g_pending[3]{};
		struct Work
		{
			std::uint64_t ticket = 0, deadline = 0;
			RE::PlayerCharacter* player = nullptr;
			std::vector<int> indices;
			std::vector<int> nativeIndices;
			std::vector<std::uint32_t> forms;
			std::vector<Entry> entries;
			void* menu = nullptr;
			RE::TESObjectREFR* owner = nullptr;
			bool barter = false;
		};
		std::unique_ptr<Work> g_queued[3], g_ready[3];  // lock protected
		std::unique_ptr<Work> g_work[3];               // TES callback only

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

		// Bounded copies on the existing TES-thread item path, never UE-thread model calls.
		bool AssetPath(const RE::BSString& a_source, char (&a_target)[1024])
		{
			__try {
				const auto size = a_source.size();
				if (size >= sizeof(a_target)) return false;
				if (size && !Read(a_source.c_str(), a_target, size)) return false;
				a_target[size] = '\0';
				return true;
			} __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
		}

		// Every native item kind gets the tag bits of the choice(s) it belongs to on its own tab. An item with no tag is
		// Ingredients and apparatus have explicit Alchemy membership, independent of effects.
		bool SubTags(RE::ItemChange* a_item, std::uint32_t& a_tags, unsigned& a_type)
		{
			__try {
				auto* object = a_item ? a_item->object : nullptr;
				if (!object) return false;
				a_tags = 0;
				a_type = static_cast<unsigned>(object->GetFormType());
				if (a_type == 34) { a_tags = itemtags::AmmoTags(a_type); return true; }
				if (a_type == 19 || a_type == 25) { a_tags = itemtags::AlchemyTags(a_type); return true; }
				if (auto* armor = object->As<RE::TESObjectARMO>()) { a_tags = itemtags::GearTags(armor->bipedModelData.bipedObjectSlots); return true; }
				if (auto* clothing = object->As<RE::TESObjectCLOT>()) { a_tags = itemtags::GearTags(clothing->bipedModelData.bipedObjectSlots); return true; }
				if (auto* weapon = object->As<RE::TESObjectWEAP>()) {
					const auto type = weapon->data.type;
					if (type == 4 || type == 5) { a_tags = itemtags::WeaponTags(type); return true; }
					char model[1024]{}, icon[1024]{};
					const bool paths = AssetPath(weapon->model, model) && AssetPath(weapon->textureName, icon);
					a_tags = paths ? itemtags::WeaponTags(type, model, icon) : itemtags::WeaponTags(type);
					return true;
				}
				if (auto* potion = object->As<RE::AlchemyItem>()) {
					std::uint32_t codes[8]{};
					unsigned count = 0;
					auto** node = reinterpret_cast<void**>(&static_cast<RE::EffectItemList*>(potion)->effectList);
					for (int n = 0; node && n < 8; ++n) {
						auto* effect = reinterpret_cast<std::uint8_t*>(node[0]);
						if (effect) codes[count++] = *reinterpret_cast<std::uint32_t*>(effect + 0x08);
						node = reinterpret_cast<void**>(node[1]);
					}
					a_tags = itemtags::PotionTags(codes, count, static_cast<RE::EffectItemList*>(potion)->hostileCount,
						potion->GetSpellType() == RE::MagicSystem::SpellType::kPoison);
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

		void Process(unsigned slot, void* containerMenu = nullptr)
		{
			if (!g_pending[slot].load(std::memory_order_acquire)) return;
			if (g_work[slot] && g_work[slot]->ticket != g_epoch[slot].load()) g_work[slot].reset();
			if (!g_work[slot]) {
				std::scoped_lock lock(g_lock);
				g_work[slot] = std::move(g_queued[slot]);
				if (!g_work[slot]) return;
			}
			auto& work = *g_work[slot];
			auto* player = RE::PlayerCharacter::GetSingleton();
			bool context = player && player == work.player && GetTickCount64() <= work.deadline;
			RE::TESObjectREFR* owner = player;
			bool barter = false;
			if (slot) {
				// Match ContainerMenu::DoEnter at 06758CB4/06758CC3 (1.512.105.0):
				// player,false versus native menu's other reference +88/barter +A9.
				context = context && containerMenu;
				if (slot == 2) {
					std::uint8_t flag = 0;
					context = context && Read(static_cast<std::uint8_t*>(containerMenu) + 0x88, &owner, sizeof(owner)) &&
						Read(static_cast<std::uint8_t*>(containerMenu) + 0xA9, &flag, sizeof(flag)) && owner && flag <= 1;
					barter = flag != 0;
				}
				if (work.entries.empty()) { work.menu = containerMenu; work.owner = owner; work.barter = barter; }
				context = context && work.menu == containerMenu && work.owner == owner && work.barter == barter;
			}
			static REL::Relocation<RE::ItemChange* (*)(RE::TESObjectREFR*, int, bool)> resolve{ REL::ID(405455) };
			static REL::Relocation<void (*)(RE::ItemChange*)> destroy{ REL::ID(407566) };
			static REL::Relocation<void (*)(void*, std::size_t)> deallocate{ REL::ID(106342) };
			const auto end = (std::min)(work.entries.size() + 32, work.indices.size());
			while (work.entries.size() < end) {
				const int index = work.indices[work.entries.size()];
				Entry entry{ index, 0, false };
				if (slot != 0) {
					// A form is optional in container/barter rows. If it is null, resolve
					// the real native index against THIS side, never the player's other list.
					const auto formID = work.forms[work.entries.size()];
					if (formID) {
						RE::ItemChange item{};
						item.object = context ? RE::TESForm::LookupByID<RE::TESBoundObject>(formID) : nullptr;
						entry.valid = Tags(&item, entry.tags);
					} else {
						const int nativeIndex = work.nativeIndices.empty() ? index : work.nativeIndices[work.entries.size()];
						auto* copy = context && nativeIndex >= 0 && nativeIndex < 1000000 ? resolve(owner, nativeIndex, barter) : nullptr;
						entry.valid = Tags(copy, entry.tags);
						if (copy) { destroy(copy); deallocate(copy, sizeof(RE::ItemChange)); }
					}
				} else {
					auto* copy = context ? resolve(player, index, false) : nullptr;
					entry.valid = Tags(copy, entry.tags);
					if (copy) { destroy(copy); deallocate(copy, sizeof(RE::ItemChange)); }
				}
				work.entries.push_back(entry);
			}
			if (work.entries.size() == work.indices.size()) {
				std::scoped_lock lock(g_lock);
				if (work.ticket == g_epoch[slot].load()) {
					g_ready[slot] = std::move(g_work[slot]);
					g_pending[slot].store(false, std::memory_order_release);
				} else g_work[slot].reset();
			}
		}

		void AfterIdle(void* a_menu)
		{
			g_original(a_menu);
			if (g_pending[0].load() && TesThread()) Process(0);
		}

		void AfterContainerIdle(void* menu)
		{
			g_originalContainer(menu);
			if ((!g_pending[1].load() && !g_pending[2].load()) || !TesThread()) return;
			Process(1, menu); Process(2, menu);
		}

		bool Install(unsigned context)
		{
			auto& original = context ? g_originalContainer : g_original;
			if (original) return true;
			REL::Relocation<std::uintptr_t> vtable{ REL::ID(context ? 1042934 : 1043093) };
			REL::Relocation<std::uintptr_t> idle{ REL::ID(context ? 410455 : 410647) };
			auto* slot = reinterpret_cast<Idle*>(vtable.address() + 0x12 * sizeof(void*));
			if (reinterpret_cast<std::uintptr_t>(*slot) != idle.address()) return false;
			DWORD previous = 0;
			if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) return false;
			original = *slot;
			InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),
				reinterpret_cast<void*>(context ? &AfterContainerIdle : &AfterIdle));
			DWORD ignored = 0;
			VirtualProtect(slot, sizeof(void*), previous, &ignored);
			return true;
		}
	}

	std::uint64_t Request(std::vector<int> a_indices, std::vector<std::uint32_t> a_forms, unsigned a_slot, std::vector<int> a_nativeIndices)
	{
		if (a_slot >= 3 || (a_slot != 0 && (a_forms.size() != a_indices.size() || a_nativeIndices.size() != a_indices.size())) ||
			a_indices.empty() || a_indices.size() > 4000 ||
			std::any_of(a_indices.begin(), a_indices.end(), [](int i) { return i < 0 || i >= 1000000; }) || !Install(a_slot)) return 0;
		std::scoped_lock lock(g_lock);
		auto work = std::make_unique<Work>();
		// Encode the slot in the low bits so Take cannot consume the other side's reply.
		work->ticket = ((g_epoch[a_slot].load() >> 2) + 1) * 4 + a_slot;
		g_epoch[a_slot].store(work->ticket);
		work->deadline = GetTickCount64() + 2000;
		work->player = RE::PlayerCharacter::GetSingleton();
		work->indices = std::move(a_indices);
		work->nativeIndices = std::move(a_nativeIndices);
		work->forms = std::move(a_forms);
		const auto ticket = work->ticket;
		g_queued[a_slot] = std::move(work);
		g_ready[a_slot].reset();
		g_pending[a_slot].store(true, std::memory_order_release);
		return ticket;
	}

	bool Take(std::uint64_t a_ticket, std::vector<Entry>& a_entries)
	{
		std::scoped_lock lock(g_lock);
		const auto slot = static_cast<unsigned>(a_ticket & 3);
		if (slot >= 3 || !g_ready[slot] || g_ready[slot]->ticket != a_ticket) return false;
		a_entries = std::move(g_ready[slot]->entries);
		g_ready[slot].reset();
		return true;
	}

	void Cancel(unsigned a_slot)
	{
		if (a_slot >= 3) return;
		std::scoped_lock lock(g_lock);
		g_epoch[a_slot].store(((g_epoch[a_slot].load() >> 2) + 1) * 4 + a_slot);
		g_queued[a_slot].reset();
		g_ready[a_slot].reset();
		g_pending[a_slot].store(false, std::memory_order_release);
	}
}
