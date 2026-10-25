# Native-style highlight (development)

Our focused header/grid cell uses opaque square borderless sRGB #E6E4DF.
Slate linear RGB: 0.7912979403 / 0.7758222183 / 0.7379104088, alpha1, outline0, radius0.
Applied filter membership does not paint another focus highlight.
Existing popup background, Kingthings font, icons and filtering remain unchanged.

Screenshot retest of91dd4af showed native A-Z's activated-sort background survives native
OnUnfocus/OnUnfocusEffect. Those calls are removed, not layered with this correction.
During custom controller header/popup ownership, mask only each native filterIcon_background
tile via RenderOpacity0. Keep glyphs, reversal arrows, native focus/selected/enabled state,
comparator and navigation indices unchanged. Restore each original opacity on ownership exit,
menu/context change or pad disconnect.

Both Magic pickers share a single per-tile snapshot so School/Effect handoff cannot capture
the already-masked zero. Slot-validated handles; exclude every custom owned wrapper.
Discover children only on ownership/context/peer refresh (empty-cache retry150ms), no global
scans. Reassert cached opacity only when nonzero. No native focus-event workaround remains.

Five host suites pass17887 checks, including201 highlight checks. This does not prove live
visual behavior; owner controller screenshot retest remains required. Actual loot/container
menus were not newly inspected. No SearchLight/MenuSearch changes.
