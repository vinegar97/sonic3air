/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/simulation/EmulatorInterface.h"


RenderParts::RenderParts() :
	mPlaneManager(mPatternManager),
	mScrollOffsetsManager(mPlaneManager),
	mSpriteManager(mPatternManager, mSpacesManager)
{
	for (int i = 0; i < 8; ++i)
		mLayerRendering[i] = true;

	reset();
}

void RenderParts::addViewport(const Recti& rect, uint16 renderQueue)
{
	Viewport& viewport = vectorAdd(mViewports);
	viewport.mRect = rect;
	viewport.mRenderQueue = renderQueue;
}

void RenderParts::reset()
{
	mActiveDisplay = true;
	mViewports.clear();

	mPlaneManager.reset();
	mSpriteManager.clear();
	mScrollOffsetsManager.reset();
}

void RenderParts::preFrameUpdate()
{
	// TODO: It could make sense to require an explicit script call for these as well, see "Renderer.resetCustomPlaneConfigurations()"
	mViewports.clear();
	mPaletteManager.preFrameUpdate();
	mSpriteManager.preFrameUpdate();
	mScrollOffsetsManager.preFrameUpdate();
}

void RenderParts::postFrameUpdate()
{
	mSpriteManager.postFrameUpdate();
	mScrollOffsetsManager.postFrameUpdate();
}

void RenderParts::refresh(const RefreshParameters& refreshParameters)
{
	if (!refreshParameters.mSkipThisFrame)
	{
		mPatternManager.refresh();
		mPlaneManager.refresh();
		mScrollOffsetsManager.refresh(refreshParameters);
	}
}

void RenderParts::dumpPatternsContent()
{
	PaletteBitmap bmp;
	mPatternManager.dumpAsPaletteBitmap(bmp);

	Color palette[0x100];
	mPaletteManager.getPalette(0).dumpColors(palette, 0x100);

	std::vector<uint8> content;
	bmp.saveBMP(content, palette);
	FTX::FileSystem->saveFile("dump.bmp", content.data(), (uint32)content.size());
}

void RenderParts::dumpPlaneContent(int planeIndex)
{
	PaletteBitmap bmp;
	mPlaneManager.dumpAsPaletteBitmap(bmp, planeIndex);

	Color palette[0x100];
	mPaletteManager.getPalette(0).dumpColors(palette, 0x100);

	std::vector<uint8> content;
	bmp.saveBMP(content, palette);
	FTX::FileSystem->saveFile("dump.bmp", content.data(), (uint32)content.size());
}
