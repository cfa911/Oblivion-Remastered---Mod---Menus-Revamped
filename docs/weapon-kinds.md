# Detailed weapon picker (development candidate)

Choices: All, Daggers, Shortswords, Longswords, Claymores, War axes, Battleaxes,
Maces, Warhammers, Bows, Arrows, Staves, Other weapons.

- Native `OBJ_WEAP.type` remains the skill/handedness boundary. Bow 5 and Staff 4
  are unchanged. Types 0/1/2/3 require compatible model/icon filename hints.
- Model/icon basenames are compared case-insensitively for dagger, shortsword,
  longsword, claymore, waraxe, battleaxe, mace and warhammer. Directory names,
  localized display names, speed/reach and fixed form IDs are not classifiers.
- A specific model hint may use an unspecified icon and vice versa. Conflicting,
  missing, wrong-handed or ambiguous hints fall under Other weapons. Custom and
  unusually named unique assets are not promised exact subtypes.
- Copies of the native paths are bounded to 1023 bytes each on the existing TES
  thread item-snapshot path, with guarded reads. No virtual model calls, scans,
  extra hooks, timers, per-item logging or new cross-mod dependency.
- The same tags apply on Weapons and All Items -> Weapons. The popup uses the
  four-column dynamic layout (four rows for thirteen choices) and input handling.
- Nine new editable `Weapon-*.png` files are provisional copies of the procedural
  Weapon-Blunt.png default for every new filter. The owner will replace them. Existing All/Bow/Staff,
  other icons and configs must not be overwritten.
- Arrows use native AMMO form 34, with Weapons category and a dedicated Arrow
  bit. No asset-name/WEAP-type inference. Weapons All and All Items -> Weapons
  include ammo; other subtypes exclude it. Weapon-Arrow.png is an editable
  Blunt-art placeholder. No native list routing or item creation is changed.
  Prior captures do not prove native Weapons excludes ammo: the later paired
  All Items/Weapons captures both had no ammo. Earlier ammo rows share UI Type 0
  with weapons. Native Weapons coverage still requires owner runtime testing.

Installed-master extraction (SHA256
`a26e21ea8c3041f8737ffb3a266129dedb7f8a88590625ecfecd5eb7f66b4a70`)
contains 1319 WEAP and 128 AMMO records. Offline hint coverage assigns 1215 weapons
to specific kinds and 104 to Other; this is not visual verification or a live
inventory test. Host tests include real master asset/type fixtures and synthetic
case/path/conflict/fallback checks. Owner runtime tests remain pending.

This candidate also retains the uncommitted SO-009 RESP/FOSP potion correction.
Development 0.0.0 only; no release or installation change implied.
