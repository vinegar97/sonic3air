/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2025 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#ifdef RMX_WITH_OPENGL_SUPPORT

#include "oxygen/rendering/opengl/shaders/RenderComponentSpriteShader.h"
#include "oxygen/rendering/opengl/OpenGLRenderResources.h"
#include "oxygen/rendering/parts/RenderParts.h"
#include "oxygen/drawing/opengl/OpenGLSpriteTextureManager.h"
#include "oxygen/helper/FileHelper.h"


void RenderComponentSpriteShader::initialize(bool alphaTest)
{
	if (FileHelper::loadShader(mShader, L"data/shader/render_sprite_component.shader", alphaTest ? "Standard_AlphaTest" : "Standard"))
	{
		bindShader();

		mLocGameResolution	= mShader.getUniformLocation("GameResolution");
		mLocPosition		= mShader.getUniformLocation("Position");
		mLocPivotOffset		= mShader.getUniformLocation("PivotOffset");
		mLocSize			= mShader.getUniformLocation("Size");
		mLocTransformation	= mShader.getUniformLocation("Transformation");
		mLocTintColor		= mShader.getUniformLocation("TintColor");
		mLocAddedColor		= mShader.getUniformLocation("AddedColor");

		mShader.setParam("SpriteTexture", 0);
	}
}

void RenderComponentSpriteShader::draw(const renderitems::ComponentSpriteInfo& spriteInfo, const Vec2i& gameResolution, OpenGLRenderResources& resources)
{
	if (nullptr == spriteInfo.mCacheItem)
		return;

	bindShader();

	// Bind textures
	{
		const OpenGLTexture* texture = OpenGLSpriteTextureManager::instance().getComponentSpriteTexture(*spriteInfo.mCacheItem);
		if (nullptr == texture)
			return;

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture->getHandle());
	}

	// Update uniforms
	{
		const PaletteManager& paletteManager = resources.getRenderParts().getPaletteManager();
		Vec4f tintColor = spriteInfo.mTintColor;
		Vec4f addedColor = spriteInfo.mAddedColor;
		if (spriteInfo.mUseGlobalComponentTint)
		{
			paletteManager.applyGlobalComponentTint(tintColor, addedColor);
		}

		if (mLastGameResolution != gameResolution)
		{
			mShader.setParam(mLocGameResolution, gameResolution);
			mLastGameResolution = gameResolution;
		}

		mShader.setParam(mLocPosition, Vec3i(spriteInfo.mInterpolatedPosition.x, spriteInfo.mInterpolatedPosition.y, spriteInfo.mPriorityFlag ? 1 : 0));
		mShader.setParam(mLocPivotOffset, spriteInfo.mPivotOffset);
		mShader.setParam(mLocSize, spriteInfo.mSize);
		mShader.setParam(mLocTransformation, spriteInfo.mTransformation.mMatrix);
		mShader.setParam(mLocTintColor, tintColor);
		mShader.setParam(mLocAddedColor, addedColor);
	}

	glDrawArrays(GL_TRIANGLES, 0, 6);
}

#endif
