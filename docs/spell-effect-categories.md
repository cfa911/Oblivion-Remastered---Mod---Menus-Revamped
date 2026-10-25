# Spell-effect categories (development)

All is unfiltered. Spells can belong to several families when their effects differ.
Other means no recognized family matched the entire spell, not an extra tag on mixed spells.
School and Effect still combine with AND. Powers remain a School/type choice rather than an effect family.

| Family | Included effects |
|---|---|
| Fire / Frost / Shock | That element's Damage effect only; not shields, resistance, weakness or summoned atronachs |
| Damage | Damage Attribute, Fatigue, Health, Magicka; Disintegrate Armor/Weapon; Sun Damage |
| Drain | Drain Attribute, Fatigue, Health, Skill, Magicka |
| Absorb | Absorb Attribute, Fatigue, Health, Skill, Magicka (not defensive Spell Absorption) |
| Restore & Cure | Restore Attribute/Fatigue/Health/Magicka; Cure Disease/Paralysis/Poison |
| Fortify | Fortify Attribute/Fatigue/Health/Skill/Magicka; Fortify Magicka Multiplier |
| Shield | Shield, Fire Shield, Frost Shield, Shock Shield |
| Resistance | Resist Disease/Fire/Frost/Magic/Normal Weapons/Paralysis/Poison/Shock |
| Weakness | Weakness to Disease/Fire/Frost/Magic/Normal Weapons/Poison/Shock |
| Summon | Explicit installed creature-summon codes plus Reanimate; no generic Z-prefix matching at runtime |
| Bound Equipment | Explicit installed Bound Armor/Weapon codes plus Mythic Dawn Armor/Helm |
| Control | Calm, Charm, Command Creature/Humanoid, Demoralize, Frenzy, Paralyze, Rally, Silence, Turn Undead |
| Stealth | Chameleon, Invisibility |
| Utility | Burden, Dispel, Detect Life, Feather, Light, Night-Eye, Open, Reflect Damage/Spell, Spell Absorption, Soul Trap, Telekinesis, Water Breathing/Walking |
| Other | Unknown/scripted effects; unused Darkness/Lock, Poison Info, Stunted Magicka and Vampirism |

Mappings verified against all 145 MGEF records in the owner's installed Remastered Oblivion.esm,
SHA256 a26e21ea8c3041f8737ffb3a266129dedb7f8a88590625ecfecd5eb7f66b4a70 (2026-10-25).
These are classification design choices, not native game categories. REMA/ABMA/FOMA/SHSH are NOT
the installed spell-effect identifiers: verified Magicka codes are RESP/ABSP/FOSP, and Shock Shield is LISH.
The earlier Restore-only change using REMA was incorrect and is superseded here.
The potion classifier is separate and is not changed by this spell-picker task.

The Effect picker is 3 columns by 6 rows (244x440), with full-name tooltips and short labels.
Inventory/School retain their existing 244x242 size. Popup position is clamped within the canvas;
extremely small canvases below 440 logical pixels tall cannot contain the full effect grid.
Controller navigation wraps through all 18 choices; the open command is negative so index 9 remains selectable.
The twelve NEW effect PNG defaults are copies of the owner's installed Weapon-Staff.png placeholder.
Existing PNGs are not overwritten by development deployment.

Known separate issue: unresolved/unsupported Magic rows still fail open and can appear under unrelated
filters. This taxonomy does not claim to fix that resolver/category problem (KB SO-007). The classifier
fixtures correctly map Warden Key to Utility, The Master's Hand/Pilgrim's Grace to Fortify,
Jone's Shadow to Stealth+Fortify, and Gates of Aetherius to Fortify+Resistance.
Serene Beauty's effect list was not established from this base-master lookup.

Run standalone host tests with an MSVC x64 prompt and C++23:
`cl /std:c++latest /EHsc tests/effect_categories_tests.cpp`, plus gear_pieces_tests and controller_policy_tests.
Host tests and an offline plugin build are not proof of native UI/runtime correctness.
