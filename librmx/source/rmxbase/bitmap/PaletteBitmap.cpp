/*
*	rmx Library
*	Copyright (C) 2017-2025 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "rmxbase.h"


namespace
{
	#pragma pack(1)
	struct BmpHeader
	{
		uint8  signature[2];
		uint32 fileSize;
		uint16 creator1;
		uint16 creator2;
		uint32 headerSize;
		uint32 dibHeaderSize;
		int32  width;
		int32  height;
		uint16 numPlanes;
		uint16 bpp;
		uint32 compression;
		uint32 dataSize;
		int32  resolutionX;
		int32  resolutionY;
		uint32 numColors;
		uint32 importantColors;
	};
	#pragma pack()

	inline uint32 swapRedBlue(uint32 color)
	{
		return (color & 0xff00ff00) | ((color & 0x00ff0000) >> 16) | ((color & 0x000000ff) << 16);
	}
}


PaletteBitmap::PaletteBitmap(const PaletteBitmap& toCopy)
{
	copy(toCopy, Recti(0, 0, toCopy.mWidth, toCopy.mHeight));
}

PaletteBitmap::~PaletteBitmap()
{
	delete[] mData;
}

void PaletteBitmap::setPixel(int x, int y, uint8 color)
{
	if (!isValidPosition(x, y))
		return;
	mData[x + y * mWidth] = color;
}

void PaletteBitmap::create(int width, int height)
{
	if (width != mWidth || height != mHeight)
	{
		delete[] mData;
		mData = new uint8[width * height];
		mWidth = width;
		mHeight = height;
	}
}

void PaletteBitmap::create(int width, int height, uint8 color)
{
	create(width, height);
	clear(color);
}

void PaletteBitmap::copy(const PaletteBitmap& source)
{
	if (nullptr == source.mData)
	{
		delete[] mData;
		mData = nullptr;
		return;
	}

	create(source.mWidth, source.mHeight);
	memcpy(mData, source.mData, mWidth * mHeight);
}

void PaletteBitmap::copy(const PaletteBitmap& source, const Recti& rect)
{
	if (nullptr == source.mData)
	{
		delete[] mData;
		mData = nullptr;
		return;
	}

	int px = rect.x;
	int py = rect.y;
	int sx = rect.width;
	int sy = rect.height;
	if (px < 0)  { sx += px;  px = 0; }
	if (py < 0)  { sy += py;  py = 0; }
	if (px + sx > (int)source.mWidth)   sx = source.mWidth - px;
	if (py + sy > (int)source.mHeight)  sy = source.mHeight - py;
	if (sx <= 0 || sy <= 0)
		return;

	create(sx, sy);
	memcpyRect(mData, mWidth, &source.mData[px+py*source.mWidth], source.mWidth, sx, sy);
}

void PaletteBitmap::copyRect(const PaletteBitmap& source, const Recti& rect, const Vec2i& destination)
{
	if (nullptr == source.mData || nullptr == mData)
		return;

	int px = rect.x;
	int py = rect.y;
	int sx = rect.width;
	int sy = rect.height;
	if (px < 0) { sx += px;  px = 0; }
	if (py < 0) { sy += py;  py = 0; }
	if (px + sx > (int)source.mWidth)   sx = source.mWidth - px;
	if (py + sy > (int)source.mHeight)  sy = source.mHeight - py;
	if (sx <= 0 || sy <= 0)
		return;

	// TODO: Add checks for destination
	memcpyRect(&mData[destination.x + destination.y * mWidth], mWidth, &source.mData[px + py * source.mWidth], source.mWidth, sx, sy);
}

void PaletteBitmap::swap(PaletteBitmap& other)
{
	std::swap(mData, other.mData);
	std::swap(mWidth, other.mWidth);
	std::swap(mHeight, other.mHeight);
}

void PaletteBitmap::clear(uint8 color)
{
	memset(mData, color, getPixelCount());
}

void PaletteBitmap::shiftAllIndices(int8 indexShift)
{
	if (indexShift == 0)
		return;

	const int pixels = getPixelCount();
	for (int i = 0; i < pixels; ++i)
	{
		mData[i] += indexShift;
	}
}

void PaletteBitmap::overwriteUnusedPaletteEntries(uint32* palette, uint32 unusedPaletteColor)
{
	bool used[0x100] = { false };
	const int pixels = getPixelCount();
	for (int i = 0; i < pixels; ++i)
	{
		used[mData[i]] = true;
	}
	for (int i = 0; i < 0x100; ++i)
	{
		if (!used[i])
			palette[i] = unusedPaletteColor;
	}
}

bool PaletteBitmap::loadBMP(const std::vector<uint8>& bmpContent, std::vector<uint32>* outPalette)
{
	VectorBinarySerializer serializer(true, bmpContent);

	// Read header
	BmpHeader header;
	serializer.read(&header, sizeof(header));
	if (memcmp(header.signature, "BM", 2) != 0)
		return false;

	// Size
	const int width = header.width;
	const int height = header.height;
	const int bitdepth = header.bpp;
	const int stride = (width * bitdepth + 31) / 32 * 4;

	// Skip unrecognized parts of the header
	if (header.dibHeaderSize > 0x28)
	{
		serializer.skip(header.dibHeaderSize - 0x28);
	}

	// Load palette
	int palSize = 0;
	if (bitdepth == 1 || bitdepth == 4 || bitdepth == 8)
	{
		palSize = (header.numColors != 0) ? header.numColors : (1 << bitdepth);
	}

	// Can't load non-palette bitmaps into a PaletteBitmap instance!
	if (palSize == 0)
		return false;

	// Read palette
	if (nullptr != outPalette)
	{
		std::vector<uint32>& palette = *outPalette;
		palette.resize(palSize);
		serializer.read(&palette[0], palSize * sizeof(uint32));
		for (int i = 0; i < palSize; ++i)
		{
			palette[i] = swapRedBlue(palette[i] | 0xff000000);
		}
	}
	else
	{
		serializer.skip(palSize * sizeof(uint32));
	}

	// Skip unrecognized parts of the header
	if (header.headerSize > serializer.getReadPosition())
	{
		serializer.skip(header.headerSize - serializer.getReadPosition());
	}

	// Create data buffer
	const int expectedSize = ((width * bitdepth + 7) / 8) * height;
	if ((int)bmpContent.size() - (int)serializer.getReadPosition() < expectedSize)
		return false;

	create(width, height);
	const uint8* buffer = &bmpContent[serializer.getReadPosition()];

	// Load image data
	for (int y = 0; y < height; ++y)
	{
		uint8* dataPtr = &mData[(height-y-1)*width];
		switch (bitdepth)
		{
			case 1:
				for (int x = 0; x < width; ++x)
					dataPtr[x] = (buffer[x/8] >> (x & 0x07)) & 0x01;
				break;

			case 4:
				for (int x = 0; x < width; ++x)
					dataPtr[x] = (buffer[x/2] >> (4 - (x & 0x01) * 4)) & 0x0f;
				break;

			case 8:
				for (int x = 0; x < width; ++x)
					dataPtr[x] = buffer[x];
				break;
		}
		buffer += stride;
	}

	return true;
}

bool PaletteBitmap::saveBMP(std::vector<uint8>& bmpContent, const uint32* palette) const
{
	VectorBinarySerializer serializer(false, bmpContent);
	const uint32 stride = (mWidth * 8 + 31) / 32 * 4;

	BmpHeader header;
	header.signature[0] = 'B';
	header.signature[1] = 'M';
	header.fileSize = sizeof(BmpHeader) + 256 * 4 + stride * mHeight;
	header.creator1 = 0;
	header.creator2 = 0;
	header.headerSize = sizeof(BmpHeader) + 256 * 4;
	header.dibHeaderSize = 40;
	header.width = mWidth;
	header.height = mHeight;
	header.numPlanes = 1;
	header.bpp = 8;
	header.compression = 0;
	header.dataSize = stride * mHeight;
	header.resolutionX = 3828;
	header.resolutionY = 3828;
	header.numColors = 256;
	header.importantColors = 256;
	serializer.write(&header, sizeof(BmpHeader));

	for (int i = 0; i < 256; ++i)
	{
		serializer.write(swapRedBlue(palette[i]) & 0x00ffffff);
	}

	for (int line = 0; line < mHeight; ++line)
	{
		serializer.write(&mData[(mHeight - 1 - line) * mWidth], mWidth);
		if (mWidth != stride)
		{
			static const uint8 zeroes[] = { 0, 0, 0, 0 };
			serializer.write(zeroes, stride - mWidth);
		}
	}
	return true;
}

void PaletteBitmap::convertToRGBA(Bitmap& output, const uint32* palette, size_t paletteSize) const
{
	output.create(mWidth, mHeight);

	const uint8* src = getData();
	uint32* dst = output.getData();

	for (int k = 0; k < getPixelCount(); ++k)
	{
		uint8 index = *src;
		*dst = (index < paletteSize) ? (0xff000000 | palette[index]) : 0;
		++src;
		++dst;
	}
}

void PaletteBitmap::memcpyRect(uint8* dst, int dwid, const uint8* src, int swid, int wid, int hgt)
{
	for (int y = 0; y < hgt; ++y)
	{
		memcpy(&dst[y*dwid], &src[y*swid], wid);
	}
}
