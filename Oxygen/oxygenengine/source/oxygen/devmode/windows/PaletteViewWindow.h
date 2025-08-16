﻿/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2025 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/devmode/ImGuiDefinitions.h"

#if defined(SUPPORT_IMGUI)

#include "oxygen/devmode/DevModeWindowBase.h"

class PaletteBase;


class PaletteViewWindow : public DevModeWindowBase
{
public:
	PaletteViewWindow();

	virtual void buildContent() override;

private:
	void drawPalette(const PaletteBase& palette);
	void savePaletteTo(const PaletteBase& palette, const std::wstring& filepath);

private:
	bool mShowSecondary = false;
};

#endif
