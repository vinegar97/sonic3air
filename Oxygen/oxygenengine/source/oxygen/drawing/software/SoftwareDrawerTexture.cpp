/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/drawing/software/SoftwareDrawerTexture.h"


void SoftwareDrawerTexture::updateFromBitmap(const Bitmap& bitmap)
{
	// Nothing to do
}

void SoftwareDrawerTexture::setupAsRenderTarget(const Vec2i& size)
{
	// Set owner's bitmap to the correct size, as we will use that for rendering into
	mOwner.accessBitmap().create(size.x, size.y);
}

void SoftwareDrawerTexture::writeContentToBitmap(Bitmap& outBitmap)
{
	outBitmap = mOwner.accessBitmap();
}

void SoftwareDrawerTexture::refreshImplementation(bool setupRenderTarget, const Vec2i& size)
{
	// Nothing to do
}
