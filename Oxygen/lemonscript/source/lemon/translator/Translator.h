/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "lemon/compiler/Node.h"


namespace lemon
{
	class Translator
	{
	public:
		static void translateToCpp(String& output, const BlockNode& rootNode);
		static void translateToCppAndSave(std::wstring_view filename, const BlockNode& rootNode);
	};
}
