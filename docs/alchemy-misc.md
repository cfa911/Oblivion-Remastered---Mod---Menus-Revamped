# Nested Alchemy and Misc Tools (development candidate)

Alchemy root: All, Potions, Ingredients, Tools.

Potions opens: All potions, Health, Magicka, Fatigue, Attributes, Cure, Poison.
Tools and Ingredients are leaves, not further menus. Apparatus (native form19)
goes into one Tools filter; ingredients (25) into Ingredients; ALCH records (40)
into Potions, even with no recognized effects. Existing tab/page mappings stay.
The same hierarchy works through All Items -> Alchemy.

- Health, Magicka and Fatigue each include Restore/Fortify effects; Magicka uses
  verified RESP/FOSP, not REMA/FOMA.
- Attributes matches the native attribute-effect codes FOAT/REAT/DGAT/DRAT/ABAT,
  separate from the derived Health/Magicka/Fatigue stats. Attribute penalties on
  wines/drinks therefore match too. No localized-name or individual attribute submenu.
- Mixed potions keep every matching family; Poison remains hostileCount > 0.
- Potions excludes ingredients/apparatus through separate native kind tags.
  Alchemy All includes all three kinds. Unresolved-row fail-open behavior is unchanged.
- Popup transitions are pure/tested independently. Parent/Back navigation never
  changes the applied filter; selecting a leaf does. Controller B/Y walks up one
  level, then closes at root; Escape closes without applying. Reopening starts at root.
- Header icons map the flat selected potion family to its submenu icon. Existing
  carousel ownership, input latches, font/background, masks and spell pickers remain.

Misc Tools: existing lockpick rule plus verified base RepairHammer form0000000C
and inventory LIGH forms (including torches). Sigil stones and other Misc remain
Other; Books/Scrolls remain together. Alchemy apparatus is not Misc Tools.
Repair-hammer overrides retain the base ID; unrelated plugin-local ID0C is not
assumed to be a hammer. Custom hammer clones with new IDs are not promised.

Four additional provisional editable PNGs: Alchemy-Potions.png,
Alchemy-Ingredients.png, Alchemy-Tools.png, Potion-Attributes.png. All thirteen new
filter placeholders are exact Weapon-Blunt.png duplicates for the owner to replace.
Do not overwrite existing customized live PNGs.

Retains the earlier detailed weapon split. Arrows remains separately queued.
No further suggested Misc changes were authorized. Development0.0.0 only.
