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
#include "oxygen/simulation/PersistentData.h"


class PersistentDataWindow : public DevModeWindowBase
{
public:
	PersistentDataWindow();
	~PersistentDataWindow();

	virtual void buildContent() override;

private:
	struct Node
	{
		std::string mName;
		const PersistentData::File* mFile = nullptr;
		std::vector<Node*> mChildNodes;
	};

private:
	void clearNode(Node& node);
	void sortNodeChildren(Node& node);
	void buildContentForNode(const Node& node);

private:
	Node mRootNode;
	RentableObjectPool<Node> mNodePool;
	uint32 mCachedChangeCounter = 0;
};

#endif
