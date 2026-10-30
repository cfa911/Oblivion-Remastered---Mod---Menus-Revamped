# Nested Alchemy and Misc Tools (development candidate)

Alchemy root: All, Potions, Poisons, Ingredients, Tools (owner regrouping 2026-10-30).

Potions opens: All potions, Restore, Fortify, Ward, Stealth, Utility, Cures, Other.
Poisons opens: All poisons, Damage, Elemental, Disable.
Tools and Ingredients are leaves, not further menus. Apparatus (native form19)
goes into one Tools filter; ingredients (25) into Ingredients; ALCH records (40)
into Potions, even with no recognized effects. Existing tab/page mappings stay.
The same hierarchy works through All Items -> Alchemy.

- Poison = native poison spell type or every effect hostile (RMI-014); everything else is a potion.
- Groups come from the effect-code table `itemtags::kAlchemyGroups`: Restore (REHE/RESP/REFA/REAT),
  Fortify (FOHE/FOSP/FOFA/FOAT/FOSK/FOMM), Ward (all Resist, SHLD, FISH/FRSH/LISH, REDG, RFLC, SABS),
  Stealth (CHML/INVI), Utility (NEYE/LGHT/DTCT/FTHR/WABR/WAWA), Cures (CUDI/CUPO/CUPA/DSPL);
  poisons: Damage (Damage/Drain Health, Magicka, Fatigue, Attribute, Drain Skill), Elemental
  (FIDG/FRDG/SHDG, WKFI/WKFR/WKSH), Disable (PARA/SLNC/BRDN).
- A potion is in every matching potion group (its hostile side effects add nothing), or Other when
  none matches (script-only quest potions). A poison is in every matching poison group, else only All poisons.
- Potions excludes ingredients/apparatus through separate native kind tags.
  Alchemy All includes all three kinds. Unresolved-row fail-open behavior is unchanged.
- Popup transitions are pure/tested independently. Parent/Back navigation never
  changes the applied filter; selecting a leaf does. Controller B/Y walks up one
  level, then closes at root; Escape closes without applying. Reopening starts at root.
- Header icons map the flat selected potion or poison group to its submenu icon. Existing
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
