#pragma once
// In-game Pack Builder Phase 4b: the live preview of the widget colour literals (bp_colour, material_colour) and of our own
// run-time tints, for the rest of the session (the Custom preset stays baked, owner decision 4). The worker
// (LivePreview.cpp) posts each literal with the four floats the bake writes (LivePreviewCore.h LiteralColour); the game thread
// (Tick) puts them on:
//   - the widget's template in the blueprint class (WidgetTree archetype), so widgets built later (list rows scrolled in,
//     menus opened later) are built in the new colour,
//   - every live widget of that class, through the property's setter (SetColorAndOpacity, SetBrushColor, ...),
//   - every live MaterialInstanceDynamic of a changed material (SetVectorParameterValue),
// only where the slot holds a colour the screen can show for that literal (stock, the colours at load, earlier previews).
// rich_row has no data row today (DT_UI_MagicMenu is not baked, RMI-086): not painted live. Log marker MR-LIVE-WIDGETS.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <string>
#include <vector>

#include "builder/LivePreviewCore.h"

namespace builder::livepreview::widgets
{
	struct Paint
	{
		pack::live::LiveLiteral lit;
		std::array<float, 4>    stock{}, baseline{}, colour{};   // the stock literal, its colour at load, the new colour
	};

	void Post(std::vector<Paint> a_paints);   // worker thread
	void Skin(const std::string& a_colourFile);   // worker thread: the newest colour file text (our own run-time tints)
	void Tick();                              // game thread, from livepreview::GameTick
}
