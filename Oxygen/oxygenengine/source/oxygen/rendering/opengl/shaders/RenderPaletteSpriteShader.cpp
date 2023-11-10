/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#ifdef RMX_WITH_OPENGL_SUPPORT

#include "oxygen/rendering/opengl/shaders/RenderPaletteSpriteShader.h"
#include "oxygen/rendering/opengl/OpenGLRenderResources.h"
#include "oxygen/rendering/Geometry.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/drawing/opengl/OpenGLSpriteTextureManager.h"
#include "oxygen/helper/FileHelper.h"


void RenderPaletteSpriteShader::initialize(bool alphaTest)
{
	const std::string additionalDefines = BufferTexture::supportsBufferTextures() ? "USE_BUFFER_TEXTURES" : "";
	FileHelper::loadShader(mShader, L"data/shader/render_sprite_palette.shader", alphaTest ? "Standard_AlphaTest" : "Standard", additionalDefines);
}

void RenderPaletteSpriteShader::refresh(const Vec2i& gameResolution, int waterSurfaceHeight, const OpenGLRenderResources& resources)
{
	mShader.bind();

	if (!mInitialized)
	{
		mLocGameResolution	= mShader.getUniformLocation("GameResolution");
		mLocWaterLevel		= mShader.getUniformLocation("WaterLevel");
		mLocPaletteTex		= mShader.getUniformLocation("PaletteTexture");
		mLocSpriteTex		= mShader.getUniformLocation("SpriteTexture");
		mLocPosition		= mShader.getUniformLocation("Position");
		mLocPivotOffset		= mShader.getUniformLocation("PivotOffset");
		mLocSize			= mShader.getUniformLocation("Size");
		mLocTransformation	= mShader.getUniformLocation("Transformation");
		mLocAtex			= mShader.getUniformLocation("Atex");
		mLocTintColor		= mShader.getUniformLocation("TintColor");
		mLocAddedColor		= mShader.getUniformLocation("AddedColor");

		glUniform1i(mLocSpriteTex, 0);
		glUniform1i(mLocPaletteTex, 1);
	}

	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, resources.mPaletteTexture.getHandle());

	if (mLastGameResolution != gameResolution || !mInitialized)
	{
		glUniform2iv(mLocGameResolution, 1, *gameResolution);
		mLastGameResolution = gameResolution;
	}

	if (mLastWaterSurfaceHeight != waterSurfaceHeight || !mInitialized)
	{
		glUniform1i(mLocWaterLevel, waterSurfaceHeight);
		mLastWaterSurfaceHeight = waterSurfaceHeight;
	}

	mInitialized = true;
}

void RenderPaletteSpriteShader::draw(const renderitems::PaletteSpriteInfo& spriteInfo, OpenGLRenderResources& resources)
{
	if (nullptr == spriteInfo.mCacheItem)
		return;

	glActiveTexture(GL_TEXTURE0);
	const BufferTexture* texture = OpenGLSpriteTextureManager::instance().getPaletteSpriteTexture(*spriteInfo.mCacheItem, spriteInfo.mUseUpscaledSprite);
	if (nullptr == texture)
		return;

	texture->bindTexture();

	glUniform3iv(mLocPosition, 1, *Vec3i(spriteInfo.mInterpolatedPosition.x, spriteInfo.mInterpolatedPosition.y, spriteInfo.mPriorityFlag ? 1 : 0));
	glUniform2iv(mLocPivotOffset, 1, *spriteInfo.mPivotOffset);
	glUniform2iv(mLocSize, 1, *spriteInfo.mSize);
	glUniform4fv(mLocTransformation, 1, *spriteInfo.mTransformation.mMatrix);
	glUniform1i (mLocAtex, spriteInfo.mAtex);
	glUniform4fv(mLocTintColor, 1, spriteInfo.mTintColor.data);
	glUniform4fv(mLocAddedColor, 1, spriteInfo.mAddedColor.data);

	glDrawArrays(GL_TRIANGLES, 0, 6);
}

#endif
