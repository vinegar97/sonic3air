/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/resources/SpriteCache.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/modding/ModManager.h"
#include "oxygen/helper/FileHelper.h"
#include "oxygen/helper/JsonHelper.h"
#include "oxygen/rendering/sprite/SpriteDump.h"
#include "oxygen/rendering/utils/Kosinski.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/LemonScriptRuntime.h"

#ifdef DEBUG
	//#define CREATE_SPRITEDUMP
#endif


namespace
{

	void decodeROMSpriteData(EmulatorInterface& emulatorInterface, std::vector<RenderUtils::PatternPixelContent>& patternBuffer, uint32 patternsBaseAddress, uint32 patternAddress, SpriteCache::ROMSpriteEncoding encoding)
	{
		switch (encoding)
		{
			case SpriteCache::ROMSpriteEncoding::NONE:
			{
				// Uncompressed / unpacked data
				const uint16 numPatterns = patternAddress;
				RenderUtils::expandMultiplePatternDataFromROM(patternBuffer, emulatorInterface.getMemoryPointer(patternsBaseAddress, false, numPatterns * 0x20), numPatterns);
				break;
			}

			case SpriteCache::ROMSpriteEncoding::CHARACTER:
			{
				// Variant for character sprites
				int numSprites = emulatorInterface.readMemory16(patternAddress);
				uint32 address = patternAddress + 2;

				for (int i = 0; i < numSprites; ++i)
				{
					const uint16 data = emulatorInterface.readMemory16(address);
					address += 2;

					uint32 src = patternsBaseAddress + (data & 0x0fff) * 0x20;
					const uint16 numPatterns = ((data & 0xf000) >> 12) + 1;

					RenderUtils::expandMultiplePatternDataFromROM(patternBuffer, emulatorInterface.getRom() + src, numPatterns);
				}
				break;
			}

			case SpriteCache::ROMSpriteEncoding::OBJECT:
			{
				// Variant for other object sprites
				int numSprites = emulatorInterface.readMemory16(patternAddress) + 1;
				uint32 address = patternAddress + 2;

				for (int i = 0; i < numSprites; ++i)
				{
					const uint16 data = emulatorInterface.readMemory16(address);
					address += 2;

					uint32 src = patternsBaseAddress + (data & 0x7ff0) * 2;
					const uint16 numPatterns = (data & 0x000f) + 1;

					RenderUtils::expandMultiplePatternDataFromROM(patternBuffer, emulatorInterface.getRom() + src, numPatterns);
				}
				break;
			}

			case SpriteCache::ROMSpriteEncoding::KOSINSKI:
			{
				// Using Kosinski compressed data
				uint8 buffer[0x1000];

				// Get the decompressed size
				uint16 size = emulatorInterface.readMemory16(patternsBaseAddress);
				if (size == 0xa000)
					size = 0x8000;
				uint32 inputAddress = patternsBaseAddress + 2;

				while (size > 0)
				{
					uint8* pointer = buffer;
					Kosinski::decompress(emulatorInterface, pointer, inputAddress);

					const uint16 bytes = std::min<uint16>(size, 0x1000);
					RMX_ASSERT((bytes & 0x1f) == 0, "Expected decompressed data size to be divisible by 0x20");
					RenderUtils::expandMultiplePatternDataFromROM(patternBuffer, buffer, bytes / 0x20);

					if (size < 0x1000)
						break;

					size -= bytes;
					inputAddress += 8;	// This is needed, but why...?
				}
				break;
			}
		}
	}

	void createPaletteSpriteFromROM(EmulatorInterface& emulatorInterface, PaletteSprite& paletteSprite, uint32 patternsBaseAddress, uint32 patternAddress, uint32 mappingAddress, SpriteCache::ROMSpriteEncoding encoding, int16 indexOffset)
	{
		// Fill sprite pattern buffer
		static std::vector<RenderUtils::PatternPixelContent> patternBuffer;
		patternBuffer.clear();
		decodeROMSpriteData(emulatorInterface, patternBuffer, patternsBaseAddress, patternAddress, encoding);

		// Fill sprite patterns
		static std::vector<RenderUtils::SinglePattern> patterns;
		patterns.clear();
		if (!patternBuffer.empty())
		{
			EmulatorInterface& emulatorInterface = EmulatorInterface::instance();
			const int count = emulatorInterface.readMemory16(mappingAddress);
			uint32 address = mappingAddress + 2;

			for (int i = 0; i < count; ++i)
			{
				RenderUtils::fillPatternsFromSpriteData(patterns, emulatorInterface.getRom() + address, patternBuffer, indexOffset);
				address += 6;
			}
		}

		// Create the palette sprite
		paletteSprite.createFromSpritePatterns(patterns);
	}

	void createPaletteSpriteFromROM(EmulatorInterface& emulatorInterface, PaletteSprite& paletteSprite, uint32 patternsBaseAddress, uint32 tableAddress, uint32 mappingOffset, uint8 animationSprite, SpriteCache::ROMSpriteEncoding encoding, int16 indexOffset)
	{
		const uint32 patternAddress = (encoding == SpriteCache::ROMSpriteEncoding::NONE || encoding == SpriteCache::ROMSpriteEncoding::KOSINSKI) ? tableAddress : (tableAddress + emulatorInterface.readMemory16(tableAddress + animationSprite * 2));
		const uint32 mappingAddress = mappingOffset + emulatorInterface.readMemory16(mappingOffset + animationSprite * 2);

		createPaletteSpriteFromROM(emulatorInterface, paletteSprite, patternsBaseAddress, patternAddress, mappingAddress, encoding, indexOffset);
	}

	bool isHexDigit(char ch)
	{
		return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
	}

}



SpriteCache::SpriteCache()
{
}

SpriteCache::~SpriteCache()
{
	clear();

	if (nullptr != mSpriteDump)
	{
		mSpriteDump->save();
		SAFE_DELETE(mSpriteDump);
	}
}

void SpriteCache::clear()
{
	// Delete the sprite instances
	for (auto& pair : mCachedSprites)
	{
		delete pair.second.mSprite;
	}
	mCachedSprites.clear();
	++mGlobalChangeCounter;
}

void SpriteCache::loadAllSpriteDefinitions()
{
	// Load or reload from all mods
	loadSpriteDefinitions(L"data/sprites");
	for (const Mod* mod : ModManager::instance().getActiveMods())
	{
		loadSpriteDefinitions(mod->mFullPath + L"sprites");
	}
}

bool SpriteCache::hasSprite(uint64 key) const
{
	return (mCachedSprites.count(key) != 0);
}

const SpriteCache::CacheItem* SpriteCache::getSprite(uint64 key)
{
	CacheItem* item = mapFind(mCachedSprites, key);
	if (nullptr != item)
	{
		// Resolve redirect
		while (nullptr != item->mRedirect)
		{
			item = item->mRedirect;
		}
		return item;
	}
	else
	{
		// Output an error
		if (EngineMain::getDelegate().useDeveloperFeatures())
		{
			const std::string_view* str = LemonScriptRuntime::tryResolveStringHash(key);
			if (nullptr != str)
			{
				RMX_ERROR("Invalid sprite cache key with string '" << *str << "'", );
			}
			else
			{
				RMX_ERROR("Invalid sprite cache key with unknown hash " << rmx::hexString(key), );
			}
		}
		return nullptr;
	}
}

SpriteCache::CacheItem& SpriteCache::getOrCreatePaletteSprite(uint64 key)
{
	CacheItem* item = mapFind(mCachedSprites, key);
	if (nullptr != item)
	{
		RMX_CHECK(!item->mUsesComponentSprite, "Sprite is not a palette sprite", );
	}
	else
	{
		item = &createCacheItem(key);
		item->mSprite = new PaletteSprite();
		item->mUsesComponentSprite = false;
	}
	return *item;
}

SpriteCache::CacheItem& SpriteCache::getOrCreateComponentSprite(uint64 key)
{
	CacheItem* item = mapFind(mCachedSprites, key);
	if (nullptr != item)
	{
		RMX_CHECK(item->mUsesComponentSprite, "Sprite is not a component sprite", );
	}
	else
	{
		item = &createCacheItem(key);
		item->mSprite = new ComponentSprite();
		item->mUsesComponentSprite = true;
	}
	return *item;
}

uint64 SpriteCache::setupSpriteFromROM(EmulatorInterface& emulatorInterface, uint32 patternsBaseAddress, uint32 tableAddress, uint32 mappingOffset, uint8 animationSprite, uint8 atex, ROMSpriteEncoding encoding, int16 indexOffset)
{
	const uint64 key = (((uint64)patternsBaseAddress) << 42) ^ (((uint64)tableAddress) << 25) ^ (((uint64)mappingOffset) << 8) ^ (uint64)animationSprite;

	CacheItem* item = mapFind(mCachedSprites, key);
	if (nullptr == item)
	{
		item = &getOrCreatePaletteSprite(key);
		PaletteSprite& paletteSprite = *static_cast<PaletteSprite*>(item->mSprite);
		createPaletteSpriteFromROM(emulatorInterface, paletteSprite, patternsBaseAddress, tableAddress, mappingOffset, animationSprite, encoding, indexOffset);

	#ifdef CREATE_SPRITEDUMP
		if (animationSprite != 0)	// TODO: Do this only for characters
		{
			String categoryKey(0, "%06x_%06x_%06x", patternsBaseAddress, tableAddress, mappingOffset);
			getSpriteDump().addSpriteWithTranslation(paletteSprite, *categoryKey, animationSprite, atex);
			item.mGotDumped = true;
		}
	#endif
	}

	return key;
}

void SpriteCache::clearRedirect(uint64 sourceKey)
{
	CacheItem* source = mapFind(mCachedSprites, sourceKey);
	if (nullptr != source)
	{
		source->mRedirect = nullptr;
	}
}

void SpriteCache::setupRedirect(uint64 sourceKey, uint64 targetKey)
{
	CacheItem* source = mapFind(mCachedSprites, sourceKey);
	if (nullptr == source)
	{
		source = &createCacheItem(sourceKey);
	}

	CacheItem* target = mapFind(mCachedSprites, targetKey);
	source->mRedirect = target;
}

SpriteDump& SpriteCache::getSpriteDump()
{
	if (nullptr == mSpriteDump)
	{
		mSpriteDump = new SpriteDump();
		mSpriteDump->load();
	}
	return *mSpriteDump;
}

void SpriteCache::dumpSprite(uint64 key, std::string_view categoryKey, uint8 spriteNumber, uint8 atex)
{
	CacheItem* item = mapFind(mCachedSprites, key);
	if (nullptr != item && !item->mGotDumped)
	{
		if (!item->mUsesComponentSprite)
		{
			const PaletteSprite& paletteSprite = *static_cast<const PaletteSprite*>(item->mSprite);
			getSpriteDump().addSprite(paletteSprite, categoryKey, spriteNumber, atex);
		}
		else
		{
			RMX_ERROR("Can't dump component sprites (attempted to dump '" << categoryKey << "' sprite " << rmx::hexString(spriteNumber, 2) << ")", );
		}
		item->mGotDumped = true;
	}
}

SpriteCache::CacheItem& SpriteCache::createCacheItem(uint64 key)
{
	CacheItem& item = mCachedSprites[key];
	item.mKey = key;
	item.mSprite = nullptr;
	item.mUsesComponentSprite = false;
	item.mChangeCounter = mGlobalChangeCounter;
	return item;
}

void SpriteCache::loadSpriteDefinitions(const std::wstring& path)
{
	struct SheetCache
	{
		std::map<std::wstring, PaletteBitmap> mPaletteSpriteSheets;
		std::map<std::wstring, Bitmap> mComponentSpriteSheets;
	};
	SheetCache sheetCache;

	std::vector<rmx::FileIO::FileEntry> fileEntries;
	fileEntries.reserve(8);
	FTX::FileSystem->listFilesByMask(path + L"/*.json", true, fileEntries);
	if (fileEntries.empty())
		return;

	++mGlobalChangeCounter;
	for (const rmx::FileIO::FileEntry& fileEntry : fileEntries)
	{
		const Json::Value spritesJson = JsonHelper::loadFile(fileEntry.mPath + fileEntry.mFilename);
		for (auto iterator = spritesJson.begin(); iterator != spritesJson.end(); ++iterator)
		{
			const String identifier(iterator.key().asString());
			uint64 key = 0;
			{
				// Check if it's an hex identifier or a string
				if (identifier.length() >= 3 && identifier[0] == '0' && identifier[1] == 'x')
				{
					bool isHex = true;
					for (int i = 2; i < identifier.length(); ++i)
					{
						if (!isHexDigit(identifier[i]))
						{
							isHex = false;
							break;
						}
					}
					if (isHex)
					{
						key = rmx::parseInteger(identifier);
					}
				}

				if (key == 0)
				{
					key = rmx::getMurmur2_64(identifier);
				}
			}

			std::wstring filename;
			Vec2i center;
			Recti rect;

			for (auto it = iterator->begin(); it != iterator->end(); ++it)
			{
				if (it.key().asString() == "File" && !it->asString().empty())
				{
					filename = *String(it->asString()).toWString();
				}
				else if (it.key().asString() == "Center" && !it->asString().empty())
				{
					std::vector<String> parts;
					String(it->asString()).split(parts, ',');
					if (parts.size() == 2)
					{
						center.x = parts[0].parseInt();
						center.y = parts[1].parseInt();
					}
				}
				else if (it.key().asString() == "Rect" && !it->asString().empty())
				{
					std::vector<String> parts;
					String(it->asString()).split(parts, ',');
					if (parts.size() == 4)
					{
						rect.x = parts[0].parseInt();
						rect.y = parts[1].parseInt();
						rect.width = parts[2].parseInt();
						rect.height = parts[3].parseInt();
					}
				}
			}

			if (!filename.empty())
			{
				// Check for overloading
				{
					const auto it = mCachedSprites.find(key);
					if (it != mCachedSprites.end())
					{
						// This sprite got overloaded e.g. by a mod -- remove the old version
						SAFE_DELETE(it->second.mSprite);
					}
				}

				CacheItem& item = createCacheItem(key);
				const std::wstring fullpath = fileEntry.mPath + filename;

				// Palette or RGBA?
				item.mUsesComponentSprite = WString(filename).endsWith(L".png");

				// Part of a sprite sheet?
				const bool isPartOfSheet = (rect.width != 0);

				bool success = false;
				if (!item.mUsesComponentSprite)
				{
					// Load palette sprite (= 8-bit palette sprite)
					PaletteSprite* sprite = new PaletteSprite();
					item.mSprite = sprite;

					if (isPartOfSheet)
					{
						PaletteBitmap* bitmap = nullptr;
						const auto it = sheetCache.mPaletteSpriteSheets.find(fullpath);
						if (it == sheetCache.mPaletteSpriteSheets.end())
						{
							bitmap = &sheetCache.mPaletteSpriteSheets[fullpath];
							success = FileHelper::loadPaletteBitmap(*bitmap, fullpath);
						}
						else
						{
							bitmap = &it->second;
							success = true;
						}

						if (success)
						{
							sprite->createFromBitmap(*bitmap, rect, -center);
						}
					}
					else
					{
						PaletteBitmap bitmap;
						success = FileHelper::loadPaletteBitmap(bitmap, fullpath);
						if (success)
						{
							static_cast<PaletteSprite*>(item.mSprite)->createFromBitmap(std::move(bitmap), -center);
						}
					}
				}
				else
				{
					// Load component sprite (= 32-bit RGBA sprite)
					ComponentSprite* sprite = new ComponentSprite();
					item.mSprite = sprite;

					if (isPartOfSheet)
					{
						Bitmap* bitmap = nullptr;
						const auto it = sheetCache.mComponentSpriteSheets.find(fullpath);
						if (it == sheetCache.mComponentSpriteSheets.end())
						{
							bitmap = &sheetCache.mComponentSpriteSheets[fullpath];
							success = FileHelper::loadBitmap(*bitmap, fullpath);
						}
						else
						{
							bitmap = &it->second;
							success = true;
						}

						if (success)
						{
							sprite->accessBitmap().copy(*bitmap, rect);
						}
					}
					else
					{
						success = FileHelper::loadBitmap(static_cast<ComponentSprite*>(item.mSprite)->accessBitmap(), fullpath);
					}
					item.mSprite->mOffset = -center;
				}
			}
		}
	}
}
