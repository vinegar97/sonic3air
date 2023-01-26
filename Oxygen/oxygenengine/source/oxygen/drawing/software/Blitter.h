/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <rmxbase.h>


class BitmapWrapper
{
public:
	inline BitmapWrapper() = default;
	inline explicit BitmapWrapper(Bitmap& bitmap) : mData(bitmap.getData()), mSize(bitmap.getSize()) {}
	inline BitmapWrapper(uint32* data, Vec2i size) : mData(data), mSize(size) {}

	inline void reset()  { mData = nullptr; mSize.set(0, 0); }
	inline void set(Bitmap& bitmap)  { mData = bitmap.getData(); mSize = bitmap.getSize(); }
	inline void set(uint32* data, Vec2i size)  { mData = data; mSize = size; }

	inline bool empty() const  { return (nullptr == mData || mSize.x <= 0 || mSize.y <= 0); }

	inline uint32* getData()  { return mData; }
	inline const uint32* getData() const  { return mData; }
	inline uint32* getPixelPointer(int x, int y)  { return &mData[x + y * mSize.x]; }
	inline const uint32* getPixelPointer(int x, int y) const  { return &mData[x + y * mSize.x]; }

	inline Vec2i getSize() const  { return mSize; }

private:
	uint32* mData = nullptr;
	Vec2i mSize;
};


class Blitter
{
public:
	struct Options
	{
		// Note: Not all options are supported everywhere; and in some cases, there's specific Blitter functions for certain options
		bool mUseAlphaBlending = false;
		bool mUseBilinearSampling = false;
		Color mTintColor = Color::WHITE;
	};

public:
	static void blitColor(BitmapWrapper& destBitmap, Recti destRect, const Color& color, const Options& options);
	static void blitBitmap(BitmapWrapper& destBitmap, Vec2i destPosition, const BitmapWrapper& sourceBitmap, Recti sourceRect, const Options& options);
	static void blitBitmapWithScaling(BitmapWrapper& destBitmap, Recti destRect, const BitmapWrapper& sourceBitmap, Recti sourceRect, const Options& options);
	static void blitBitmapWithUVs(BitmapWrapper& destBitmap, Recti destRect, const BitmapWrapper& sourceBitmap, Recti sourceRect, const Options& options);
};
