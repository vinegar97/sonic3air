/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen/application/menu/MenuItems.h"
#include <rmxmedia.h>


class OxygenMenu : public GuiBase
{
public:
	OxygenMenu();
	~OxygenMenu();

	virtual void initialize() override;
	virtual void deinitialize() override;
	virtual void update(float timeElapsed) override;
	virtual void render() override;

private:
	MenuItemContainer mMenuItems;
};
