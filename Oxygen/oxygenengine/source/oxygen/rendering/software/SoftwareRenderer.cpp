/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/software/SoftwareRenderer.h"
#include "oxygen/rendering/software/SoftwareBlur.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/drawing/Drawer.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/drawing/software/BlitterHelper.h"


namespace detail
{
	class PixelBlockWriter
	{
	public:
		PixelBlockWriter(SoftwareRenderer::BufferedPlaneData& data, const PatternManager::CacheItem* patternCache) :
			mBufferedPlaneData(&data),
			mContent(&data.mContent[0]),
			mPatternCache(patternCache)
		{}

		void newLine(int lineNumber, int position, int paletteIndex)
		{
			mLineNumber = lineNumber;
			mPosition = position;
			mPaletteIndex = paletteIndex;
			mLastPatternBits = 0xffff;
		}

		FORCE_INLINE void addPixels(int x, uint16 patternIndex, int pixels)
		{
			const PatternManager::CacheItem::Pattern& pattern = mPatternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
			uint8* dst = &mContent[mPosition + x];
			const uint8* srcPatternPixels = &pattern.mPixels[mPatternPixelOffset];
			memcpy(dst, srcPatternPixels, pixels);

			const uint16 patternBits = (patternIndex & 0xe000);		// Includes priority bit and atex
			if (mLastPatternBits != patternBits)
			{
				mLastPatternBits = patternBits;

				mCurrentPixelBlock = &vectorAdd((patternBits & 0x8000) ? mBufferedPlaneData->mPrioBlocks : mBufferedPlaneData->mNonPrioBlocks);
				mCurrentPixelBlock->mStartCoords.set(x, mLineNumber);
				mCurrentPixelBlock->mLinearPosition = mPosition + x;
				mCurrentPixelBlock->mNumPixels = pixels;
				mCurrentPixelBlock->mAtex = (patternIndex >> 9) & 0x30;
				mCurrentPixelBlock->mPaletteIndex = mPaletteIndex;
			}
			else
			{
				mCurrentPixelBlock->mNumPixels += pixels;
			}
		}

		FORCE_INLINE void addPixels8(int x, uint16 patternIndex)
		{
			// Same as above, but with hardcoded "pixels == 8"
			const PatternManager::CacheItem::Pattern& pattern = mPatternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
			uint64* dst = (uint64*)&mContent[mPosition + x];
			const uint64* srcPatternPixels = (uint64*)&pattern.mPixels[mPatternPixelOffset];
			*dst = *srcPatternPixels;

			const uint16 patternBits = (patternIndex & 0xe000);		// Includes priority bit and atex
			if (mLastPatternBits != patternBits)
			{
				mLastPatternBits = patternBits;

				mCurrentPixelBlock = &vectorAdd((patternBits & 0x8000) ? mBufferedPlaneData->mPrioBlocks : mBufferedPlaneData->mNonPrioBlocks);
				mCurrentPixelBlock->mStartCoords.set(x, mLineNumber);
				mCurrentPixelBlock->mLinearPosition = mPosition + x;
				mCurrentPixelBlock->mNumPixels = 8;
				mCurrentPixelBlock->mAtex = (patternIndex >> 9) & 0x30;
				mCurrentPixelBlock->mPaletteIndex = mPaletteIndex;
			}
			else
			{
				mCurrentPixelBlock->mNumPixels += 8;
			}
		}

	public:
		int mPatternPixelOffset = 0;

	private:
		SoftwareRenderer::BufferedPlaneData* mBufferedPlaneData = nullptr;
		uint8* mContent = nullptr;
		const PatternManager::CacheItem* mPatternCache = nullptr;

		int mLineNumber = 0;
		int mPosition = 0;
		int mDepthPosition = 0;
		int mPaletteIndex = 0;

		SoftwareRenderer::BufferedPlaneData::PixelBlock* mCurrentPixelBlock = nullptr;
		uint16 mLastPatternBits = 0xffff;
	};
}


SoftwareRenderer::SoftwareRenderer(RenderParts& renderParts, DrawerTexture& outputTexture) :
	Renderer(RENDERER_TYPE_ID, renderParts, outputTexture)
{
}

void SoftwareRenderer::initialize()
{
	mGameResolution = Configuration::instance().mGameScreen;
	mGameScreenTexture.accessBitmap().create(mGameResolution.x, mGameResolution.y);
}

void SoftwareRenderer::reset()
{
	clearGameScreen();
}

void SoftwareRenderer::setGameResolution(const Vec2i& gameResolution)
{
	if (mGameResolution != gameResolution)
	{
		mGameResolution = gameResolution;
		mGameScreenTexture.accessBitmap().create(mGameResolution.x, mGameResolution.y);
	}
}

void SoftwareRenderer::clearGameScreen()
{
	mGameScreenTexture.accessBitmap().clear(0xff000000);
	mGameScreenTexture.bitmapUpdated();
}

void SoftwareRenderer::renderGameScreen(const std::vector<Geometry*>& geometries)
{
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	// Clear depth buffer
	memset(mDepthBuffer, 0, sizeof(mDepthBuffer));
	mEmptyDepthBuffer = true;

	if (mRenderParts.getEnforceClearScreen())
	{
		gameScreenBitmap.clear(0);
	}

	mCurrentViewport.set(0, 0, mGameResolution.x, mGameResolution.y);
	mFullViewport = true;

	for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
	{
		mBufferedPlaneData[i].mValid = false;
	}

	// Do some analysis on what's to render
	bool usingSpriteMask = false;
	{
		for (const Geometry* geometry : geometries)
		{
			if (geometry->getType() == Geometry::Type::SPRITE && geometry->as<SpriteGeometry>().mSpriteInfo.getType() == SpriteManager::SpriteInfo::Type::MASK)
			{
				usingSpriteMask = true;
				break;
			}
		}
	}

	// Render geometries
	{
		uint16 lastRenderQueue = 0xffff;
		for (size_t i = 0; i < geometries.size(); ++i)
		{
			const uint16 renderQueue = geometries[i]->mRenderQueue;
			if (usingSpriteMask && lastRenderQueue < 0x8000 && renderQueue >= 0x8000)
			{
				// Copy planes (needed for sprite masking)
				mGameScreenCopy = gameScreenBitmap;
			}

			renderGeometry(*geometries[i]);
			lastRenderQueue = renderQueue;
		}
	}

	// Set alpha channel to 0xff to make sure nothing gets lost due to alpha test
	{
		uint64* RESTRICT ptr = (uint64*)gameScreenBitmap.getData();
		uint64* RESTRICT end = ptr + gameScreenBitmap.getPixelCount() / 2;
		for (; ptr < end; ++ptr)
		{
			*ptr |= 0xff000000ff000000ull;
		}
	}

	mGameScreenTexture.bitmapUpdated();
}

void SoftwareRenderer::renderDebugDraw(int debugDrawMode, const Recti& rect)
{
	// TODO: This whole function and how GameView interacts with it, could be improved on (it's a bit of a mess...)

	Drawer& drawer = EngineMain::instance().getDrawer();
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();
	const Vec2i oldSize = gameScreenBitmap.getSize();

	Vec2i bitmapSize;
	if (debugDrawMode <= PlaneManager::PLANE_A)
	{
		bitmapSize = mRenderParts.getPlaneManager().getPlayfieldSizeInPixels();
	}
	else
	{
		bitmapSize.set(512, 256);
	}
	mGameScreenTexture.setupAsRenderTarget(bitmapSize.x, bitmapSize.y);
	gameScreenBitmap.create(bitmapSize.x, bitmapSize.y, 0);

	mCurrentViewport.set(0, 0, bitmapSize.x, bitmapSize.y);
	mFullViewport = true;

	// Render to bitmap
	{
		const PlaneManager& planeManager = mRenderParts.getPlaneManager();
		const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
		const PatternManager& patternManager = mRenderParts.getPatternManager();
		const uint32* palettes[2] = { paletteManager.getPalette(0).getData(), paletteManager.getPalette(1).getData() };
		const PatternManager::CacheItem* patternCache = patternManager.getPatternCache();
		const uint16 numPatternsPerLine = (uint16)(bitmapSize.x / 8);
		const bool highlightPrioPatterns = (FTX::keyState(SDLK_LSHIFT) != 0);

		for (int y = 0; y < bitmapSize.y; ++y)
		{
			uint32* destRGBA = gameScreenBitmap.getPixelPointer(0, y);
			const uint32* palette = (y < paletteManager.mSplitPositionY) ? palettes[0] : palettes[1];

			for (int x = 0; x < bitmapSize.x; )
			{
				const uint16 patternIndex = planeManager.getPatternAtIndex(debugDrawMode, (x / 8) + (y / 8) * numPatternsPerLine);
				const PatternManager::CacheItem::Pattern& pattern = patternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];
				const uint8* srcPatternPixels = &pattern.mPixels[(x & 0x07) + (y & 0x07) * 8];
				const uint8 atex = (patternIndex >> 9) & 0x30;

				for (int k = 0; k < 8; ++k)
				{
					const uint8 colorIndex = srcPatternPixels[k];
					destRGBA[x+k] = 0xff000000 | palette[colorIndex + atex];
				}

				const bool lowerBrightness = (highlightPrioPatterns && (patternIndex & 0x8000) == 0);
				if (lowerBrightness)
				{
					for (int k = 0; k < 8; ++k)
					{
						destRGBA[x+k] = 0xff000000 | ((destRGBA[x+k] & 0x00fcfcfc) >> 2);
					}
				}

				x += 8;
			}
		}
	}
	mGameScreenTexture.bitmapUpdated();

	drawer.setWindowRenderTarget(FTX::screenRect());
	drawer.setBlendMode(BlendMode::OPAQUE);
	drawer.drawUpscaledRect(RenderUtils::getLetterBoxRect(rect, (float)bitmapSize.x / (float)bitmapSize.y), mGameScreenTexture);
	drawer.performRendering();

	mGameScreenTexture.setupAsRenderTarget(oldSize.x, oldSize.y);
	gameScreenBitmap.create(oldSize.x, oldSize.y);
}

void SoftwareRenderer::renderGeometry(const Geometry& geometry)
{
	switch (geometry.getType())
	{
		case Geometry::Type::UNDEFINED:
			break;	// This should never happen anyways

		case Geometry::Type::PLANE:
		{
			renderPlane(static_cast<const PlaneGeometry&>(geometry));
			break;
		}

		case Geometry::Type::SPRITE:
		{
			renderSprite(static_cast<const SpriteGeometry&>(geometry));
			break;
		}

		case Geometry::Type::RECT:
		{
			const RectGeometry& rg = static_cast<const RectGeometry&>(geometry);
			mBlitter.blitColor(Blitter::OutputWrapper(mGameScreenTexture.accessBitmap(), rg.mRect), rg.mColor, BlendMode::ALPHA);
			break;
		}

		case Geometry::Type::TEXTURED_RECT:
		{
			const TexturedRectGeometry& tg = static_cast<const TexturedRectGeometry&>(geometry);
			Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

			Blitter::Options blitterOptions;
			blitterOptions.mBlendMode = BlendMode::ALPHA;
			blitterOptions.mTintColor = &tg.mColor;

			mBlitter.blitSprite(Blitter::OutputWrapper(mGameScreenTexture.accessBitmap()), Blitter::SpriteWrapper(tg.mDrawerTexture.accessBitmap(), Vec2i()), tg.mRect.getPos(), blitterOptions);
			break;
		}

		case Geometry::Type::EFFECT_BLUR:
		{
			const EffectBlurGeometry& ebg = static_cast<const EffectBlurGeometry&>(geometry);
			Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

			if (ebg.mBlurValue >= 1)
			{
				SoftwareBlur::blurBitmap(gameScreenBitmap, ebg.mBlurValue);
			}
			break;
		}

		case Geometry::Type::VIEWPORT:
		{
			const ViewportGeometry& vg = static_cast<const ViewportGeometry&>(geometry);
			const Recti fullViewport(0, 0, mGameResolution.x, mGameResolution.y);
			mCurrentViewport = fullViewport;
			mCurrentViewport.intersect(vg.mRect);
			mFullViewport = (mCurrentViewport == fullViewport);
			break;
		}
	}
}

void SoftwareRenderer::renderPlane(const PlaneGeometry& geometry)
{
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	Recti rect(0, 0, mGameResolution.x, mGameResolution.y);
	rect.intersect(geometry.mActiveRect);
	const int minX = rect.x;
	const int maxX = rect.x + rect.width;
	const int minY = rect.y;
	const int maxY = rect.y + rect.height;

	const PlaneManager& planeManager = mRenderParts.getPlaneManager();
	const ScrollOffsetsManager& scrollOffsetsManager = mRenderParts.getScrollOffsetsManager();
	const PaletteManager& paletteManager = mRenderParts.getPaletteManager();

	// Search for already setup buffered plane data fitting this geometry
	int foundFittingBufferedPlaneDataIndex = -1;
	for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
	{
		const BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[i];
		if (bufferedPlaneData.mValid &&
			bufferedPlaneData.mPlaneIndex == geometry.mPlaneIndex &&
			bufferedPlaneData.mScrollOffsets == geometry.mScrollOffsets &&
			bufferedPlaneData.mActiveRect == geometry.mActiveRect)
		{
			foundFittingBufferedPlaneDataIndex = i;
			break;
		}
	}

	if (foundFittingBufferedPlaneDataIndex == -1)
	{
		// Find a free index
		for (int i = 0; i < MAX_BUFFER_PLANE_DATA; ++i)
		{
			if (!mBufferedPlaneData[i].mValid)
			{
				foundFittingBufferedPlaneDataIndex = i;
				break;
			}
		}
		RMX_CHECK(foundFittingBufferedPlaneDataIndex != -1, "No free buffered plane data structure found", return);

		BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[foundFittingBufferedPlaneDataIndex];
		bufferedPlaneData.mPlaneIndex = geometry.mPlaneIndex;
		bufferedPlaneData.mScrollOffsets = geometry.mScrollOffsets;
		bufferedPlaneData.mActiveRect = geometry.mActiveRect;
		bufferedPlaneData.mContent.resize(gameScreenBitmap.getPixelCount());
		bufferedPlaneData.mPrioBlocks.clear();
		bufferedPlaneData.mPrioBlocks.reserve(0x800);
		bufferedPlaneData.mNonPrioBlocks.clear();
		bufferedPlaneData.mNonPrioBlocks.reserve(0x800);

		const uint16* planeData = planeManager.getPlaneDataInVRAM(geometry.mPlaneIndex);
		const uint16 numPatternsPerLine = (geometry.mPlaneIndex <= PlaneManager::PLANE_A) ? planeManager.getPlayfieldSizeInPatterns().x : 64;
		const uint16* scrollOffsetsH = nullptr;
		const uint16* scrollOffsetsV = nullptr;
		uint16 scrollMaskH = 0xff;
		uint16 scrollMaskV = 0;
		bool scrollNoRepeat = false;

		if (geometry.mPlaneIndex == PlaneManager::PLANE_W)
		{
			static uint16 wScrollOffsetX;
			wScrollOffsetX = (uint16)scrollOffsetsManager.getPlaneWScrollOffset().x;
			scrollOffsetsH = &wScrollOffsetX;
			scrollMaskH = 0;
		}
		else
		{
			scrollOffsetsH = scrollOffsetsManager.getScrollOffsetsH(geometry.mScrollOffsets);
			scrollOffsetsV = scrollOffsetsManager.getScrollOffsetsV(geometry.mScrollOffsets);
			scrollMaskV = scrollOffsetsManager.getVerticalScrolling() ? 0x1f : 0;
			scrollNoRepeat = scrollOffsetsManager.getHorizontalScrollNoRepeat(geometry.mScrollOffsets);
		}
		const uint16 positionMaskH = planeManager.getPlayfieldSizeInPixels().x - 1;
		const uint16 positionMaskV = planeManager.getPlayfieldSizeInPixels().y - 1;
		const int16 verticalScrollOffsetBias = scrollOffsetsManager.getVerticalScrollOffsetBias();

		detail::PixelBlockWriter pixelBlockWriter(bufferedPlaneData, mRenderParts.getPatternManager().getPatternCache());

		for (int y = minY; y < maxY; ++y)
		{
			const int position = y * gameScreenBitmap.getWidth();
			pixelBlockWriter.newLine(y, position, (y < paletteManager.mSplitPositionY) ? 0 : 1);

			int vx = minX;
			if (nullptr != scrollOffsetsH)
				vx += (int16)scrollOffsetsH[y & scrollMaskH];

			int startX = minX;
			int endX = maxX;
			if (scrollNoRepeat)
			{
				if (vx < 0)
				{
					startX -= vx;
					vx = 0;
				}
				else if (endX > startX + (positionMaskH - vx))
				{
					endX = startX + (positionMaskH - vx) + 1;
				}
				if (startX >= endX)
					continue;
			}

			if (scrollMaskV == 0)
			{
				// Optimized version of the code below in the else-block
				const int vy = ((nullptr == scrollOffsetsV) ? y : (y + scrollOffsetsV[0])) & positionMaskV;
				const uint16* planeDataForThisLine = &planeData[(vy / 8) * numPatternsPerLine];
				const int patternPixelBaseOffset = (vy & 0x07) * 8;

				// First few pixels until vx gets divisible by 8
				int x = startX;
				{
					vx &= positionMaskH;
					const uint16 patternIndex = planeDataForThisLine[vx / 8];
					const int vxMod8 = vx & 0x07;
					const int pixels = std::min(8 - vxMod8, endX - x);
					pixelBlockWriter.mPatternPixelOffset = patternPixelBaseOffset + vxMod8;
					pixelBlockWriter.addPixels(x, patternIndex, pixels);
					x += pixels;
					vx += pixels;
				}

				// Full blocks of 8 pixels
				pixelBlockWriter.mPatternPixelOffset = patternPixelBaseOffset;
				while (true)
				{
					vx &= positionMaskH;
					const int remainingVx = (positionMaskH - vx + 1);
					const int numPixels = std::min(remainingVx, endX - x) / 8 * 8;
					if (numPixels < 8)
						break;

					const uint16* planeDataPointer = &planeDataForThisLine[vx / 8];
					const int localEndX = x + numPixels;
					while (x < localEndX)
					{
						const uint16 patternIndex = *planeDataPointer;
						pixelBlockWriter.addPixels8(x, patternIndex);
						x += 8;
						++planeDataPointer;
					}
					vx += numPixels;
				}

				// Remaining pixels on the right
				if (x < endX)
				{
					vx &= positionMaskH;
					const uint16 patternIndex = planeDataForThisLine[vx / 8];
					const int pixels = endX - x;
					pixelBlockWriter.addPixels(x, patternIndex, pixels);
				}
			}
			else
			{
				for (int x = startX; x < endX; )
				{
					vx &= positionMaskH;

					int vy;
					if (nullptr == scrollOffsetsV)
					{
						vy = y;
					}
					else
					{
						const int verticalScrollOffset = (scrollMaskV == 0) ? scrollOffsetsV[0] : scrollOffsetsV[((x - verticalScrollOffsetBias) >> 4) & scrollMaskV];
						vy = (y + verticalScrollOffset) & positionMaskV;
					}

					const uint16 patternIndex = planeData[(vx / 8) + (vy / 8) * numPatternsPerLine];
					const int vxMod8 = vx & 0x07;
					const int pixels = std::min(8 - vxMod8, endX - x);

					pixelBlockWriter.mPatternPixelOffset = vxMod8 + (vy & 0x07) * 8;
					pixelBlockWriter.addPixels(x, patternIndex, pixels);
					x += pixels;
					vx += pixels;
				}
			}
		}

		bufferedPlaneData.mValid = true;
	}

	// Write plane data to output
	{
		BufferedPlaneData& bufferedPlaneData = mBufferedPlaneData[foundFittingBufferedPlaneDataIndex];

		const uint32* palettes[2] = { paletteManager.getPalette(0).getData(), paletteManager.getPalette(1).getData() };
		const bool isBackground = (geometry.mPlaneIndex == PlaneManager::PLANE_B && !geometry.mPriorityFlag);

		const std::vector<BufferedPlaneData::PixelBlock>& blocks = geometry.mPriorityFlag ? bufferedPlaneData.mPrioBlocks : bufferedPlaneData.mNonPrioBlocks;
		for (const BufferedPlaneData::PixelBlock& block : blocks)
		{
			const uint8* RESTRICT src = &bufferedPlaneData.mContent[block.mLinearPosition];
			uint32* RESTRICT dstRGBA = &gameScreenBitmap.getData()[block.mLinearPosition];
			const uint32* RESTRICT paletteWithAtex = &palettes[block.mPaletteIndex][block.mAtex];

			if (isBackground)
			{
				for (int i = 0; i < block.mNumPixels; ++i)
				{
					dstRGBA[i] = paletteWithAtex[src[i]];
				}
			}
			else if (geometry.mPriorityFlag)
			{
				uint8* RESTRICT dstDepth = &mDepthBuffer[block.mStartCoords.x + block.mStartCoords.y * 0x200];
				for (int i = 0; i < block.mNumPixels; ++i)
				{
					if (src[i] & 0x0f)
					{
						dstRGBA[i] = paletteWithAtex[src[i]];
						dstDepth[i] = 0x80;
					}
				}
			}
			else
			{
				for (int i = 0; i < block.mNumPixels; ++i)
				{
					if (src[i] & 0x0f)
					{
						dstRGBA[i] = paletteWithAtex[src[i]];
					}
				}
			}
		}

		if (!blocks.empty() && geometry.mPriorityFlag)
			mEmptyDepthBuffer = false;
	}
}

void SoftwareRenderer::renderSprite(const SpriteGeometry& geometry)
{
	Bitmap& gameScreenBitmap = mGameScreenTexture.accessBitmap();

	switch (geometry.mSpriteInfo.getType())
	{
		case SpriteManager::SpriteInfo::Type::VDP:
		{
			const SpriteManager::VdpSpriteInfo& sprite = static_cast<const SpriteManager::VdpSpriteInfo&>(geometry.mSpriteInfo);

			const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
			const uint32* palettes[2] = { paletteManager.getPalette(0).getData(), paletteManager.getPalette(1).getData() };
			const PatternManager::CacheItem* patternCache = mRenderParts.getPatternManager().getPatternCache();

			const uint8 depthValue = (sprite.mPriorityFlag) ? 0x80 : 0;
			const bool useTintColor = (sprite.mTintColor != Color::WHITE || sprite.mAddedColor != Color::TRANSPARENT);

			Recti rect(sprite.mInterpolatedPosition.x, sprite.mInterpolatedPosition.y, sprite.mSize.x * 8, sprite.mSize.y * 8);
			rect.intersect(mCurrentViewport);

			const int minX = rect.x;
			const int maxX = rect.x + rect.width;
			const int minY = rect.y;
			const int maxY = rect.y + rect.height;

			for (int y = minY; y < maxY; ++y)
			{
				const uint32* palette = (y < paletteManager.mSplitPositionY) ? palettes[0] : palettes[1];

				for (int x = minX; x < maxX; ++x)
				{
					// Depth test
					if (depthValue < mDepthBuffer[x + y * 0x200])
						continue;

					int vx = x - sprite.mInterpolatedPosition.x;
					int vy = y - sprite.mInterpolatedPosition.y;

					int patternX = vx / 8;
					int patternY = vy / 8;
					if (sprite.mFirstPattern & 0x0800)
						patternX = sprite.mSize.x - patternX - 1;
					if (sprite.mFirstPattern & 0x1000)
						patternY = sprite.mSize.y - patternY - 1;

					const uint16 patternIndex = sprite.mFirstPattern + patternY + patternX * sprite.mSize.y;
					const PatternManager::CacheItem::Pattern& pattern = patternCache[patternIndex & 0x07ff].mFlipVariation[(patternIndex >> 11) & 3];

					uint8 colorIndex = pattern.mPixels[(vx%8) + (vy%8) * 8];
					colorIndex += (patternIndex >> 9) & 0x30;
					if (colorIndex & 0x0f)
					{
						uint32& dst = *gameScreenBitmap.getPixelPointer(x, y);
						if (useTintColor)
						{
							Color color = Color::fromABGR32(palette[colorIndex]);
							color.r = saturate(sprite.mAddedColor.r + color.r * sprite.mTintColor.r);
							color.g = saturate(sprite.mAddedColor.g + color.g * sprite.mTintColor.g);
							color.b = saturate(sprite.mAddedColor.b + color.b * sprite.mTintColor.b);
							color.a = saturate(sprite.mAddedColor.a + color.a * sprite.mTintColor.a);
							uint32 src = color.getABGR32();
							BlitterHelper::blendPixelAlpha((uint8*)&dst, (uint8*)&src);
						}
						else
						{
							dst = palette[colorIndex];
						}
					}
				}
			}
			break;
		}

		case SpriteManager::SpriteInfo::Type::PALETTE:
		case SpriteManager::SpriteInfo::Type::COMPONENT:
		{
			// Shared code for palette & component sprite rendering
			const SpriteManager::CustomSpriteInfoBase& spriteBase = static_cast<const SpriteManager::CustomSpriteInfoBase&>(geometry.mSpriteInfo);
			const bool isPaletteSprite = (geometry.mSpriteInfo.getType() == SpriteManager::SpriteInfo::Type::PALETTE);

			const PaletteManager& paletteManager = mRenderParts.getPaletteManager();
			BitmapViewMutable<uint8> depthBufferView(mDepthBuffer, Vec2i(0x200, 0x100));	// Depth buffer uses a fixed size...

			// Build blitter options
			Blitter::Options blitterOptions;
			Color tintColor = spriteBase.mTintColor;
			Color addedColor = spriteBase.mAddedColor;
			{
				if (spriteBase.mUseGlobalComponentTint && !isPaletteSprite)
				{
					tintColor.r *= paletteManager.getGlobalComponentTintColor().r;
					tintColor.g *= paletteManager.getGlobalComponentTintColor().g;
					tintColor.b *= paletteManager.getGlobalComponentTintColor().b;
					tintColor.a *= paletteManager.getGlobalComponentTintColor().a;
					addedColor += paletteManager.getGlobalComponentAddedColor();
				}

				const bool hasTransform = !spriteBase.mTransformation.isIdentity();
				blitterOptions.mTransform = hasTransform ? *spriteBase.mTransformation.mMatrix : nullptr;
				blitterOptions.mInvTransform = hasTransform ? *spriteBase.mTransformation.mInverse : nullptr;
				blitterOptions.mSamplingMode = SamplingMode::POINT;
				blitterOptions.mBlendMode = spriteBase.mBlendMode;
				blitterOptions.mTintColor = (tintColor != Color::WHITE) ? &tintColor : nullptr;
				blitterOptions.mAddedColor = (addedColor != Color::TRANSPARENT) ? &addedColor : nullptr;
				blitterOptions.mDepthBuffer = (mEmptyDepthBuffer && !spriteBase.mPriorityFlag) ? nullptr : &depthBufferView;
				blitterOptions.mDepthTestValue = (spriteBase.mPriorityFlag) ? 0x80 : 0;
			}

			if (isPaletteSprite)
			{
				// Palette sprite specific code
				const SpriteManager::PaletteSpriteInfo& spriteInfo = static_cast<const SpriteManager::PaletteSpriteInfo&>(spriteBase);

				const PaletteSprite& paletteSprite = *static_cast<PaletteSprite*>(spriteInfo.mCacheItem->mSprite);
				const PaletteBitmap& paletteBitmap = spriteInfo.mUseUpscaledSprite ? paletteSprite.getUpscaledBitmap() : paletteSprite.getBitmap();
				const Blitter::IndexedSpriteWrapper spriteWrapper(paletteBitmap.getData(), paletteBitmap.getSize(), -paletteSprite.mOffset);
				const Blitter::PaletteWrapper paletteWrapper(paletteManager.getPalette(0).getData() + spriteInfo.mAtex, paletteManager.getPalette(0).getSize() - spriteInfo.mAtex);

				// Handle screen palette split
				const int splitY = paletteManager.mSplitPositionY;
				if (splitY < mGameResolution.y)
				{
					const Blitter::PaletteWrapper paletteWrapper2(paletteManager.getPalette(1).getData() + spriteInfo.mAtex, paletteManager.getPalette(1).getSize() - spriteInfo.mAtex);

					Recti targetRect = Recti::getIntersection(mCurrentViewport, Recti(0, 0, mGameResolution.x, splitY));
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);

					targetRect = Recti::getIntersection(mCurrentViewport, Recti(0, splitY, mGameResolution.x, mGameResolution.y - splitY));
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, targetRect), spriteWrapper, paletteWrapper2, spriteInfo.mInterpolatedPosition, blitterOptions);
				}
				else
				{
					mBlitter.blitIndexed(Blitter::OutputWrapper(gameScreenBitmap, mCurrentViewport), spriteWrapper, paletteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);
				}
			}
			else
			{
				// Component sprite specific code
				const SpriteManager::ComponentSpriteInfo& spriteInfo = static_cast<const SpriteManager::ComponentSpriteInfo&>(spriteBase);

				const ComponentSprite& componentSprite = *static_cast<ComponentSprite*>(spriteInfo.mCacheItem->mSprite);
				const Blitter::SpriteWrapper spriteWrapper(componentSprite.getBitmap(), -componentSprite.mOffset);

				mBlitter.blitSprite(Blitter::OutputWrapper(gameScreenBitmap, mCurrentViewport), spriteWrapper, spriteInfo.mInterpolatedPosition, blitterOptions);
			}

			if (spriteBase.mPriorityFlag)
				mEmptyDepthBuffer = false;
			break;
		}

		case SpriteManager::SpriteInfo::Type::MASK:
		{
			// Overwrite sprites with plane rendering results in given rect
			const SpriteManager::SpriteMaskInfo& mask = static_cast<const SpriteManager::SpriteMaskInfo&>(geometry.mSpriteInfo);
			if (mask.mSize.x > 0 && mask.mSize.y > 0)
			{
				const int minX = clamp(mask.mInterpolatedPosition.x, 0, gameScreenBitmap.getWidth());
				const int maxX = clamp(mask.mInterpolatedPosition.x + mask.mSize.x, 0, gameScreenBitmap.getWidth());
				const int bytes = (maxX - minX) * 4;
				if (bytes > 0)
				{
					const int minY = clamp(mask.mInterpolatedPosition.y, 0, gameScreenBitmap.getHeight());
					const int maxY = clamp(mask.mInterpolatedPosition.y + mask.mSize.y, 0, gameScreenBitmap.getHeight());

					for (int line = minY; line < maxY; ++line)
					{
						const uint32 offset = minX + line * gameScreenBitmap.getWidth();
						memcpy(&gameScreenBitmap[offset], &mGameScreenCopy[offset], bytes);
					}
				}
			}
			break;
		}

		case SpriteManager::SpriteInfo::Type::INVALID:
			break;
	}
}
