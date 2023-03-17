/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#ifdef RMX_WITH_OPENGL_SUPPORT

#include "oxygen/rendering/utils/PaletteBitmap.h"
#include "oxygen/rendering/utils/BufferTexture.h"
#include "oxygen/drawing/opengl/OpenGLTexture.h"

class RenderParts;


class OpenGLRenderResources
{
public:
	OpenGLRenderResources(RenderParts& renderParts);

	void initialize();
	void refresh();
	void clearAllCaches();

	const BufferTexture& getHScrollOffsetsTexture(int scrollOffsetsIndex) const;
	const BufferTexture& getVScrollOffsetsTexture(int scrollOffsetsIndex) const;

public:
	RenderParts& mRenderParts;

	// Palette
	Bitmap		  mPaletteBitmap;
	OpenGLTexture mPaletteTexture;

	// Patterns
	PaletteBitmap mPatternCacheBitmap;
	BufferTexture mPatternCacheTexture;
	bool mAllPatternsDirty = true;

	// Planes
	BufferTexture mPlanePatternsTexture[4];
	uint16 mPlanePatternsData[4][0x1000] = { 0 };	// Cache of last uploaded data, to be able to make comparisons

	// Scrolling
	BufferTexture mHScrollOffsetsTexture[4];	// First two are for the planes, the others are used for certain effects that require an additional set of scroll offsets
	BufferTexture mVScrollOffsetsTexture[4];
	BufferTexture mEmptyScrollOffsetsTexture;
};

#endif
