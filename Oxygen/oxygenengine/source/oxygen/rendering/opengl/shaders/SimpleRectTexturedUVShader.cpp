/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2025 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"

#ifdef RMX_WITH_OPENGL_SUPPORT

#include "oxygen/rendering/opengl/shaders/SimpleRectTexturedUVShader.h"
#include "oxygen/helper/FileHelper.h"


void SimpleRectTexturedUVShader::initialize(bool supportsTintColor, const char* techname)
{
	mSupportsTintColor = supportsTintColor;
	if (FileHelper::loadShader(mShader, L"data/shader/simple_rect_textured_uv.shader", techname))
	{
		bindShader();

		mLocTransform = mShader.getUniformLocation("Transform");
		mLocTexture   = mShader.getUniformLocation("MainTexture");

		if (mSupportsTintColor)
		{
			mLocTintColor = mShader.getUniformLocation("TintColor");
		}
	}
}

void SimpleRectTexturedUVShader::setup(GLuint textureHandle, const Vec4f& transform, const Color& tintColor)
{
	bindShader();

	// Bind textures
	mShader.setTexture("MainTexture", textureHandle, GL_TEXTURE_2D);

	// Update uniforms
	{
		mShader.setParam(mLocTransform, transform);

		if (mSupportsTintColor)
		{
			mShader.setParam(mLocTintColor, tintColor);
		}
	}
}

#endif
