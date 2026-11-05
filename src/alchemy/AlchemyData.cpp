#include "alchemy/AlchemyInternal.h"

namespace alchemy
{
	namespace detail
	{
		long long       NowMs() { return static_cast<long long>(GetTickCount64()); }

		bool Enabled() { return mrconfig::Get().alchemy.enabled; }

		// ---- reflection helpers
		UE::UObject* Field(UE::UObject* a_obj, std::string_view a_name)
		{
			if (!a_obj || !a_obj->GetClass()) return nullptr;
			const auto off = reflect::Offset(a_obj->GetClass(), a_name);
			if (off < 0) return nullptr;
			auto* p = *reflect::At<UE::UObject*>(a_obj, off);
			return p && reflect::IsLive(p) ? p : nullptr;
		}

		bool Flag(UE::UObject* a_obj, std::string_view a_name)
		{
			const auto off = a_obj && a_obj->GetClass() ? reflect::Offset(a_obj->GetClass(), a_name) : -1;
			return off >= 0 && *reflect::At<bool>(a_obj, off);
		}

		UE::UObject* InnerList(UE::UObject* a_wrapper) { return Field(a_wrapper, "ListView"); }

		std::vector<UE::UObject*> ListItems(UE::UObject* a_list)
		{
			std::vector<UE::UObject*> out;
			ui::Call g(a_list, L"GetListItems");
			void*    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || !g.Run()) return out;
			auto*      data = *reinterpret_cast<UE::UObject***>(ret);
			const auto num = *reinterpret_cast<std::int32_t*>(static_cast<std::uint8_t*>(ret) + 8);
			for (std::int32_t i = 0; data && i < num && i < 4000; ++i) {
				if (data[i]) out.push_back(data[i]);
			}
			if (data) UE::FMemory::Free(data);
			return out;
		}

		std::vector<UE::UObject*> DisplayedRows(UE::UObject* a_list)
		{
			std::vector<UE::UObject*> out;
			ui::Call shown(a_list, L"GetDisplayedEntryWidgets");
			const auto* rows = shown && shown.Run() ? static_cast<const RawArray*>(shown.At("ReturnValue")) : nullptr;
			auto* const* data = rows ? reinterpret_cast<UE::UObject* const*>(rows->data) : nullptr;
			for (std::int32_t i = 0; data && i < rows->num && i < 64; ++i) {
				if (data[i]) out.push_back(data[i]);
			}
			return out;
		}

		void SetItems(UE::UObject* a_list, const std::vector<UE::UObject*>& a_items)
		{
			// UListView copies this const input during the call. Avoid publishing a transient empty list.
			ui::Call batch(a_list, L"BP_SetListItems");
			const auto count = static_cast<std::int32_t>(a_items.size());
			const RawArray items{ reinterpret_cast<std::uint8_t*>(const_cast<UE::UObject**>(a_items.data())), count, count };
			if (batch && batch.Size("InListItems") == sizeof(items) && batch.Set("InListItems", items) && batch.Run()) return;
			ui::Call clear(a_list, L"ClearListItems");
			if (!clear) return;
			clear.Run();
			for (auto* item : a_items) {
				ui::Call add(a_list, L"AddItem");
				add.Set("Item", item);
				add.Run();
			}
			ui::Call refresh(a_list, L"RequestRefresh");
			if (refresh) refresh.Run();
		}

		// a function of a_class whose name holds every part (bound-event names carry generated numbers)
		std::wstring FindFunction(UE::UClass* a_class, std::initializer_list<std::string_view> a_parts)
		{
			for (auto* s = static_cast<UE::UStruct*>(a_class); s; s = s->superStruct) {
				int guard = 0;
				for (auto* f = s->children.Get(); f && ++guard < 2000; f = f->next) {
					const auto name = pe::Utf8(f->GetFName().ToString());
					bool all = true;
					for (auto part : a_parts) all &= name.find(part) != std::string::npos;
					if (all) return std::wstring(name.begin(), name.end());
				}
				break;   // the Blueprint class itself declares its bound events
			}
			return {};
		}

		bool ReadPointer(const std::uint8_t* a_address, const std::uint8_t** a_out)
		{
			__try {
				*a_out = *reinterpret_cast<const std::uint8_t* const*>(a_address);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool CopyName(const UE::FName* a_name, std::string* a_out) { *a_out = pe::Utf8(a_name->ToString()); return true; }

		bool NameGuarded(const UE::FName* a_name, std::string* a_out)
		{
			__try {
				return CopyName(a_name, a_out);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// RMI-023: the struct an array property holds (FArrayProperty::Inner -> FStructProperty::Struct), not an assumed
		// one. The inner property carries the array's own name; the struct must be a live ScriptStruct. Offsets of the two
		// links are tried in the range UE 5.x uses and proven by those checks.
		UE::UStruct* ArrayStruct(UE::UStruct* a_owner, std::string_view a_array)
		{
			const auto* field = reflect::FindField(a_owner, a_array);
			if (!field) return nullptr;
			static auto* scriptStruct = ui::Class(L"/Script/CoreUObject.ScriptStruct");
			for (std::ptrdiff_t innerOff : { 0x70, 0x78, 0x80 }) {
				const std::uint8_t* inner = nullptr;
				if (!ReadPointer(field + innerOff, &inner) || !inner || reinterpret_cast<std::uintptr_t>(inner) < 0x10000) continue;
				std::string innerName;
				if (!NameGuarded(reinterpret_cast<const UE::FName*>(inner + 0x20), &innerName) || innerName != a_array) continue;
				for (std::ptrdiff_t structOff : { 0x70, 0x78, 0x80 }) {
					const std::uint8_t* candidate = nullptr;
					if (!ReadPointer(inner + structOff, &candidate) || !candidate) continue;
					auto* obj = reinterpret_cast<UE::UObject*>(const_cast<std::uint8_t*>(candidate));
					if (reflect::IsLive(obj) && obj->GetClass() == scriptStruct) {
						return reinterpret_cast<UE::UStruct*>(obj);
					}
				}
			}
			return nullptr;
		}

		const PropLayout& Props()
		{
			static PropLayout l;
			static bool   tried = false;
			if (tried) return l;
			tried = true;
			auto* props = reinterpret_cast<UE::UStruct*>(UE::StaticFindObject<UE::UObject>(nullptr, nullptr, L"/Script/Altar.LegacyIngredientItemProperties"));
			auto* effect = props ? ArrayStruct(props, "Effects") : nullptr;   // RMI-023: the struct the array really holds
			if (!props || !effect) {
				logger::warn("alchemy: ingredient structs not found; sort and counts are off");
				return l;
			}
			l.size = props->propertiesSize;
			l.name = reflect::Offset(props, "NameText");
			l.quantity = reflect::Offset(props, "Quantity");
			l.effects = reflect::Offset(props, "Effects");
			l.effectSize = effect->propertiesSize;
			std::vector<FieldInfo> fields;
			std::string            list;
			for (auto& [name, offset] : reflect::Fields(effect)) {
				fields.push_back({ name, offset, ui::SizeOf(effect, name) });
				list += name + "+" + std::to_string(offset) + "/" + std::to_string(fields.back().size) + " ";
			}
			const auto [flag, inverted] = UnknownFlag(fields);
			l.effectUnknown = flag;   // -1 when the struct has no such flag
			l.unknownInverted = inverted;
			l.effectName = TextField(fields, static_cast<int>(sizeof(UE::FText)));
			l.ok = l.size > 0 && l.name >= 0 && l.quantity >= 0 && l.effects >= 0 && l.effectSize > 0 && l.effectName >= 0 &&
			       l.effectName + static_cast<std::int32_t>(sizeof(UE::FText)) <= l.effectSize &&
			       ui::SizeOf(props, "NameText") == static_cast<std::int32_t>(sizeof(UE::FText));
			if (!l.ok) logger::warn("alchemy: ingredient struct layout unexpected (size {:X}, effect size {:X}); sort and counts are off", l.size, l.effectSize);
			return l;
		}

		// RMI-021: an FText read from game data - plausible pointers first, then the engine's ToString under a fault guard
		bool CopyText(const UE::FText* a_text, std::string* a_out) { *a_out = reflect::Text(*a_text); return true; }

		bool TextGuarded(const UE::FText* a_text, std::string* a_out)
		{
			__try {
				return CopyText(a_text, a_out);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		bool ReadQword(unsigned long long a_address, unsigned long long* a_out)
		{
			__try {
				*a_out = *reinterpret_cast<const unsigned long long*>(a_address);
				return true;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// the game executable's image (vtables of live engine objects lie in it)
		std::pair<unsigned long long, unsigned long long> GameImage()
		{
			static std::pair<unsigned long long, unsigned long long> s_image{ 0, 0 };
			if (!s_image.first) {
				const auto base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
				const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
				s_image = { reinterpret_cast<unsigned long long>(base), nt->OptionalHeader.SizeOfImage };
			}
			return s_image;
		}

		bool SafeText(const UE::FText* a_text, std::string& a_out)
		{
			const auto* raw = reinterpret_cast<const unsigned long long*>(a_text);
			if (!a_text || !PlausibleText(raw[0], raw[1])) return false;
			unsigned long long vtable = 0;   // RMI-022: only a live text object - its vtable lies in the game image
			const auto [imageBase, imageSize] = GameImage();
			if (!ReadQword(raw[0], &vtable) || !InImage(vtable, imageBase, imageSize)) return false;
			if (!TextGuarded(a_text, &a_out)) {
				static bool s_said = false;
				if (!s_said) { s_said = true; logger::warn("alchemy: an ingredient text faulted when read; skipped"); }
				return false;
			}
			return true;
		}

		void AddIngredientForm(RE::TESForm* a_form)
		{
			auto* ingredient = a_form ? a_form->As<RE::IngredientItem>() : nullptr;
			if (!ingredient) return;
			const char* key = RE::TESFullName::GetFullName(a_form);   // the name's string-table key, "LOC_FN_..."
			if (key) alchemyrules::AddIngredient(g_ingredients, key, ingredient->weight, ingredient->data.costOverride);
		}

		bool WalkForms()
		{
			auto* forms = RE::TESForm::GetAllForms();
			if (!forms) return false;
			for (auto it = forms->begin(); it != forms->end(); ++it) AddIngredientForm(it->second);
			return true;
		}

		bool WalkFormsGuarded()   // no unwinding objects here (C2712): the walk itself is in WalkForms
		{
			__try {
				return WalkForms();
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		// IMP-MR-005: the editor-ID map holds no ingredients at run time (2026-11-04); the full-name key does (as for
		// Inventory rows). Built once: ingredient forms do not change after the game's data is loaded.
		void BuildIngredientIndex()
		{
			if (g_ingredientsBuilt) return;
			g_ingredientsBuilt = true;
			g_ingredients.clear();
			const bool ok = WalkFormsGuarded();
			if (!ok || g_ingredients.empty()) logger::warn("alchemy: no ingredient forms indexed (walk {}); Weight/Value sort by name", ok ? "empty" : "faulted");
		}

		std::pair<float, int> FormData(const std::string& a_name, const UE::FText& a_text)
		{
			if (auto it = g_formData.find(a_name); it != g_formData.end()) return it->second;
			BuildIngredientIndex();
			const auto data = alchemyrules::IngredientStats(g_ingredients, reflect::TextKey(a_text));   // "LOC_FN_..."
			g_formData.emplace(a_name, data);
			g_formMatch.emplace(MatchKey(a_name), data);
			return data;
		}

		bool ReadIngredient(const std::uint8_t* a_props, Ingredient& a_out, bool a_formData, bool a_effects)
		{
			const auto& l = Props();
			if (!l.ok || !a_props) return false;
			const auto* name = reinterpret_cast<const UE::FText*>(a_props + l.name);
			if (!SafeText(name, a_out.name)) return false;
			a_out.count = *reinterpret_cast<const std::int32_t*>(a_props + l.quantity);
			const auto* effects = reinterpret_cast<const RawArray*>(a_props + l.effects);
			a_out.knownEffects.clear();
			for (std::int32_t i = 0; a_effects && effects->data && i < effects->num && i < 8; ++i) {
				const auto* e = effects->data + static_cast<std::ptrdiff_t>(i) * l.effectSize;
				if (l.effectUnknown >= 0 && *reinterpret_cast<const bool*>(e + l.effectUnknown) != l.unknownInverted) continue;   // unknown
				std::string effect;
				if (SafeText(reinterpret_cast<const UE::FText*>(e + l.effectName), effect)) a_out.knownEffects.push_back(std::move(effect));
			}
			if (a_formData) {
				const auto [weight, value] = FormData(a_out.name, *name);
				a_out.weight = weight;
				a_out.value = value;
			}
			return true;
		}

		// ---- effect names of filter list items
		bool LooksLikeText(const std::uint8_t* a_p)
		{
			const auto a = *reinterpret_cast<const std::uint64_t*>(a_p);
			const auto b = *reinterpret_cast<const std::uint64_t*>(a_p + 8);
			return a > 0x10000 && b == a - 0x10;   // TSharedRef object + its controller allocated with it
		}

		bool EffectItemName(UE::UObject* a_item, std::string& a_out)
		{
			static UE::UClass* s_class = nullptr;
			if (!a_item || !a_item->GetClass()) return false;
			if (!s_class) {
				if (pe::Utf8(a_item->GetClass()->GetFName().ToString()) != "VModernAlchemyEffect") return false;
				s_class = a_item->GetClass();
			}
			if (a_item->GetClass() != s_class) return false;
			if (g_nameOffset < 0) return false;
			return SafeText(reinterpret_cast<const UE::FText*>(reinterpret_cast<const std::uint8_t*>(a_item) + g_nameOffset), a_out) && !a_out.empty();
		}

		int CountOf(const std::string& a_display)
		{
			const auto it = g_counts.find(MatchKey(a_display));
			return it == g_counts.end() ? 0 : it->second;
		}

		// ---- IMP-MR-004: counts
		void ComputeCounts(UE::UObject* a_menu)
		{
			g_dirtyCounts = false;
			auto* vm = Field(a_menu, "VAlchemyMenuViewModel");
			const auto& l = Props();
			const auto  off = vm ? reflect::Offset(vm->GetClass(), "ItemsList") : -1;
			if (!vm || !l.ok || off < 0) return;
			const auto* arr = reflect::At<RawArray>(vm, off);
			const bool needCounts = NeedCounts(g_countsOpened, g_countsNum, arr->num);
			g_countsOpened = false;
			g_countsNum = arr->num;
			logger::flush();
			std::vector<Ingredient> all;
			for (std::int32_t i = 0; arr->data && i < arr->num && i < 2000; ++i) {
				Ingredient item;
				if (ReadIngredient(arr->data + static_cast<std::ptrdiff_t>(i) * l.size, item, false)) all.push_back(std::move(item));
			}
			// Native rebuilds may reveal a newly learned effect without changing the ingredient count.
			decltype(g_markerEffects) markerEffects;
			for (const auto& item : all) markerEffects[MatchKey(item.name)] = item.knownEffects;
			if (markerEffects != g_markerEffects) g_effectSearchDirty = true;
			g_markerEffects = std::move(markerEffects);
			if (!needCounts) return;
			g_counts = EffectCounts(all);
			static bool s_said = false;
			if (!s_said && all.size() >= 5 && g_counts.empty()) {
				s_said = true;
				logger::warn("alchemy: no known effects read from {} ingredients; filter counts show 0", all.size());
			}
			g_countsReady = true;
		}

		std::vector<std::pair<int, std::string>> ItemTexts(UE::UObject* a_item)
		{
			std::vector<std::pair<int, std::string>> texts;
			if (!a_item || !reflect::IsLive(a_item) || !a_item->GetClass()) return texts;
			if (a_item->GetClass()->propertiesSize < kEffectNameCandidate + static_cast<std::int32_t>(sizeof(UE::FText))) return texts;
			std::string s;
			if (SafeText(reinterpret_cast<const UE::FText*>(reinterpret_cast<const std::uint8_t*>(a_item) + kEffectNameCandidate), s))
				texts.emplace_back(kEffectNameCandidate, std::move(s));
			return texts;
		}

		bool RootSearchSource(UE::UObject* a_menu, const std::vector<UE::UObject*>& a_items, reflect::Handle& a_cache, const wchar_t* a_name)
		{
			auto* cache = reflect::Get(a_cache);
			if (!cache || !ui::IsWithin(cache, a_menu)) {
				cache = ui::FindInTree(a_menu, textutil::Utf8(a_name));
				if (!cache || !ui::IsWithin(cache, a_menu)) {
					auto* canvas = ui::TreeRootCanvas(a_menu);
					UE::UObject* slot = nullptr;
					cache = canvas ? ui::AddToCanvas(canvas, L"/Script/UMG.ListView", a_name, &slot) : nullptr;
				}
				if (!cache) return false;
				ui::Visible(cache, false);
				a_cache = reflect::Hold(cache);
			}
			// The hidden ListView roots the original UObjects while visible matches are removed.
			ui::Call clear(cache, L"ClearListItems");
			if (!clear || !clear.Run()) return false;
			for (auto* item : a_items) ui::CallFirst(cache, L"AddItem", &item, sizeof(item));
			return true;
		}

		bool RootSearchItems(UE::UObject* a_menu, const std::vector<UE::UObject*>& a_items)
		{
			return RootSearchSource(a_menu, a_items, g_searchCache, L"MrAlchemySearchSource");
		}

		bool MatchesQuery(const Ingredient& a_item, const std::string& a_query)
		{
			const bool ignoreAccents = searchcfg::Get().ignoreAccents;
			return IngredientMatches(a_item, a_query, [&](std::string_view a_text, std::string_view a_q) { return textutil::Contains(a_text, a_q, ignoreAccents); });
		}

		void ApplySort(UE::UObject* a_menu)
		{
			g_dirtySort = false;
			auto* list = InnerList(Field(a_menu, "IngredientsListView"));
			if (!list) return;
			const auto live = ListItems(list);
			if (auto* cache = reflect::Get(g_searchCache); !cache || !ui::IsWithin(cache, a_menu)) g_searchList.Reset();
			if (g_searchList.Observe(live) && !RootSearchItems(a_menu, live)) { g_searchList.Reset(); return; }
			const auto& items = g_searchList.source;
			if (items.empty()) { g_searchList.Commit(live); return; }
			const auto propsOff = reflect::Offset(items.front()->GetClass(), "Properties");
			if (propsOff < 0) return;
			std::vector<Ingredient> data(items.size());
			const auto query = searchcfg::Get().alchemy && searchcfg::Get().ownFilter ? search::GetText(menus::Menu::kAlchemy) : std::string();
			const bool effects = !query.empty(); // search reads known effects; sorting alone does not
			logger::flush();
			for (std::size_t i = 0; i < items.size(); ++i) {
				if (!ReadIngredient(reinterpret_cast<const std::uint8_t*>(items[i]) + propsOff, data[i], true, effects)) return;   // all or nothing
			}
			static bool s_warned = false;
			if (!s_warned && data.size() >= 5 && std::none_of(data.begin(), data.end(), [](const Ingredient& i) { return i.weight >= 0; })) {
				s_warned = true;
				logger::warn("alchemy: no ingredient weight/value found (name key lookup); those sorts fall back to the name");
			}
			auto order = SortOrder(data, g_sortState.key, g_sortState.descending);
			std::erase_if(order, [&](int i) { return !MatchesQuery(data[static_cast<std::size_t>(i)], query); });
						std::vector<UE::UObject*> sorted;
			sorted.reserve(order.size());
			for (int i : order) sorted.push_back(items[static_cast<std::size_t>(i)]);
			g_searchList.Commit(sorted);
			if (sorted == live) return;
			UE::UObject* selected = nullptr;
			{
				ui::Call get(list, L"BP_GetSelectedItem");
				if (get && get.Run()) selected = get.Get<UE::UObject*>("ReturnValue");
			}
			SetItems(list, sorted);
			if (selected && std::find(sorted.begin(), sorted.end(), selected) != sorted.end()) {
				ui::Call set(list, L"BP_SetSelectedItem");
				if (set) { set.Set("Item", selected); set.Run(); }
			}
		}

		// ---- IMP-MR-006: the selected filters
		bool LooksLikeFString(const std::uint8_t* a_elem)
		{
			__try {
				const auto* data = *reinterpret_cast<const wchar_t* const*>(a_elem);
				const auto  num = *reinterpret_cast<const std::int32_t*>(a_elem + 8);
				const auto  max = *reinterpret_cast<const std::int32_t*>(a_elem + 12);
				return data && num >= 1 && num <= 512 && max >= num && data[num - 1] == 0;
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				return false;
			}
		}

		std::vector<std::string> SelectedNames(UE::UObject* a_menu)
		{
			std::vector<std::string> out;
			auto*    vm = Field(a_menu, "VAlchemyMenuViewModel");
			ui::Call g(vm, L"GetEffectFiltersSelectedNames");
			void*    ret = g ? g.At("ReturnValue") : nullptr;
			if (!ret || g.Size("ReturnValue") != 16 || !g.Run()) return out;
			auto* arr = static_cast<RawArray*>(ret);
			if (!arr->data || arr->num <= 0) return out;
			static int s_kind = 0;   // 1 FString elements, 2 FText elements, -1 neither
			if (s_kind == 0) {
				s_kind = LooksLikeFString(arr->data) ? 1 : LooksLikeText(arr->data) ? 2 : -1;
				if (s_kind < 0) logger::warn("alchemy: selected filter names have an unknown type; the active-filter line is off");
			}
			for (std::int32_t i = 0; s_kind > 0 && i < arr->num && i < 64; ++i) {
				if (s_kind == 1) {
					auto* s = reinterpret_cast<UE::FString*>(arr->data + static_cast<std::ptrdiff_t>(i) * 16);
					out.push_back(pe::Utf8(*s));
					s->~FString();
				} else {
					auto* t = reinterpret_cast<UE::FText*>(arr->data + static_cast<std::ptrdiff_t>(i) * sizeof(UE::FText));
					std::string name;
					if (SafeText(t, name)) out.push_back(std::move(name));
					t->~FText();
				}
			}
			UE::FMemory::Free(arr->data);
			arr->data = nullptr;
			arr->num = arr->max = 0;
			return out;
		}

		UE::UObject* FindFilterItem(UE::UObject* a_list, const std::string& a_key)
		{
			for (auto* item : ListItems(a_list)) {
				std::string name;
				if (EffectItemName(item, name) && MatchKey(name) == a_key) return item;
			}
			return nullptr;
		}

		bool ClickTab(UE::UObject* a_filters, bool a_positive)
		{
			const auto fn = FindFunction(a_filters->GetClass(), { a_positive ? "PositiveEffectsButton" : "NegativeEffectsButton", "Clicked" });
			auto*      button = Field(a_filters, a_positive ? "PositiveEffectsButton" : "NegativeEffectsButton");
			return !fn.empty() && button && ui::CallFirst(a_filters, fn.c_str(), &button, sizeof(button));
		}

		// turn one filter off the way a click on its row does; the item may sit on the other tab
		void RemoveFilter(UE::UObject* a_menu, const std::string& a_display)
		{
			auto* filters = Field(a_menu, "Alchemy_Filters");
			auto* list = InnerList(Field(filters, "ListView_Filters"));
			if (!filters || !list || g_itemNames != 1) return;
			const auto click = FindFunction(filters->GetClass(), { "ListView_Filters", "OnItemClicked" });
			if (click.empty()) return;
			const auto key = MatchKey(a_display);
			auto*      item = FindFilterItem(list, key);
			bool       switched = false;
			const bool positive = Flag(filters, "bIsOnPostiveEffect");
			if (!item && ClickTab(filters, positive)) {
				g_effectSearchDirty = true;
				item = FindFilterItem(list, key);
			}
			if (!item && ClickTab(filters, !positive)) {
				switched = true;
				item = FindFilterItem(list, key);
			}
			if (item) ui::CallFirst(filters, click.c_str(), &item, sizeof(item));
			if (switched) ClickTab(filters, positive);
		}

		// Clear all: each filter off as a single entry does (the menu's clear-all input-event stub does nothing when called)
		void RemoveEveryFilter(UE::UObject* a_menu)
		{
			for (const auto& name : SelectedNames(a_menu)) RemoveFilter(a_menu, name);
		}



		void ApplyEffectSearch(UE::UObject* a_filters)
		{
			if (!g_effectSearchReady) return;
			auto* list = InnerList(Field(a_filters, "ListView_Filters"));
			if (!list) return;
			std::vector<UE::UObject*> matches;
			for (auto* item : g_effectSearchItems) {
				std::string name;
				if (g_effectSearchQuery.empty() || !EffectItemName(item, name) ||
					textutil::Contains(name, g_effectSearchQuery, searchcfg::Get().ignoreAccents)) matches.push_back(item);
			}
			if (ListItems(list) != matches) SetItems(list, matches);
		}

		void ShowFilterTab(UE::UObject* a_filters, FilterTab a_tab)
		{
			auto* list = InnerList(Field(a_filters, "ListView_Filters"));
			if (!list) return;
			g_settingTab = true;
			// Read fresh native lists: no long-lived raw object cache, and zero-count
			// filtering cannot permanently remove entries from the combined source.
			ClickTab(a_filters, false);
			auto negative = ListItems(list);
			ClickTab(a_filters, true);
			auto positive = ListItems(list);
			if (a_tab == FilterTab::Negative) ClickTab(a_filters, false);
			const auto source = alchemytabs::Items(a_tab, positive, negative);
			g_effectSearchReady = RootSearchSource(Widget(), source, g_effectSearchCache, L"MrAlchemyFilterSearchSource");
			g_effectSearchItems = g_effectSearchReady ? source : std::vector<UE::UObject*>{};
			SetItems(list, source);
			ApplyEffectSearch(a_filters);
			static bool announced = false;
			if (!announced) { announced = true; logger::warn("alchemy: All / Positive / Negative recreated filter tabs ready (inward ornaments and full effect labels)"); }
			g_settingTab = false;
			g_filterTab = a_tab;
			g_nextLayout = {};
		}
	}
}
