# Native-style highlight (development)

Owner reference: sRGB#E6E4DF, opaque fill, no outline and square corners on our focused header/grid cell.
Use linear Slate0.7912979403/0.7758222183/0.7379104088/1; border width0, fixed corner radius0.
Only focus painting changes: selected filter membership never paints a second persistent highlight.
The accepted popup paper/background, native item/spell font and icon images are unchanged.
Ui::RoundedBox optional radius defaults6 so all existing unrelated calls retain their previous appearance.

On controller ownership ENTER (our header or open controller popup), invoke OnUnfocus on this carousel's
visible live native inner buttons, then its inherited OnUnfocusEffect when available. Do this once per
transition, not every input poll. Cache peer wrappers only during existing header registration; exclude
all owned custom headers before reading filterIcon so no uninitialized custom native Filter is touched.
Retained objects are slot-validated, all calls guarded on the game thread.

On ownership LEAVE, native navigation normally restores focus. For a mouse-opened popup used with a pad,
restore OnFocus only for the actual native focused wrapper if the carousel still owns sort focus and its
inner GetIsFocused isfalse. No duplicate focus event when normal navigation already did it.
No direct sort/reversal/selection/enable-array/navigation-index edits; no broad event suppression.

The game's callable signatures are in prior captured metadata. The inherited visual-event behavior
and complete live handoff require owner testing; host tests are not proof of that UI behavior.
Popup interpreted as the owner's "container"; actual loot/container menus were not newly inspected.
Filtering, corrected power membership,18-choice layout, icons, settings and SearchLight ABI remain unchanged.
