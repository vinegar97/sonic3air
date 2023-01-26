/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <rmxmedia.h>

class Mod;


class FontCollection : public SingleInstance<FontCollection>
{
public:
	struct Definition
	{
		std::wstring mDefinitionFile;
		const Mod* mMod = nullptr;
	};
	struct CollectedFont
	{
		std::string mKeyString;
		uint64 mKeyHash = 0;
		std::vector<Definition> mDefinitions;	// There can be multiple font definitions with the same key, thanks to overloading

		int mLoadedDefinitionIndex = -1;
		FontSourceBitmap* mFontSource = nullptr;
		Font mUnmodifiedFont;
		std::vector<Font*> mManagedFonts;
	};

public:
	Font* getFontByKey(uint64 keyHash);

	void reloadAll();
	void collectFromMods();

	void registerManagedFont(Font& font, const std::string& key);

private:
	void loadDefinitionsFromPath(std::wstring_view path, const Mod* mod);
	void updateLoadedFonts();

private:
	std::unordered_map<uint64, CollectedFont> mCollectedFonts;	// Using "mKeyHash" as map key
};
