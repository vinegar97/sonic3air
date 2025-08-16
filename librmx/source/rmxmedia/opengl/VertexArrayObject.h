/*
*	rmx Library
*	Copyright (C) 2008-2025 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*
*	VertexArrayObject
*		Support for OpenGL VAOs.
*/

#pragma once

#ifdef RMX_WITH_OPENGL_SUPPORT

namespace opengl
{
	class VertexArrayObject
	{
	public:
		enum class Format
		{
			UNDEFINED,
			P2,			// 2D position
			P2_C3,		// 2D position, RGB color
			P2_C4,		// 2D position, RGBA color
			P2_T2,		// 2D position, 2D texcoords
			P3_C3,		// 3D position, RGB color
			P3_N3_C3,	// 3D position, normal vector, RGB color
						// ...add more as needed
		};

	public:
		~VertexArrayObject();

		inline bool isValid() const  { return mVertexBufferObjectHandle != 0; }
		void setup(Format format);

		inline size_t getNumBufferedVertices() const  { return mNumBufferedVertices; }
		void updateVertexData(const float* vertexData, size_t numVertices);

		void bind();
		void unbind() const;

		void draw(GLenum mode);		// Shortcut for "bind()" + "glDrawArrays(mode, 0, mNumBufferedVertices)"

	private:
		void applyCurrentFormat();

	private:
		GLuint mVertexBufferObjectHandle = 0;	// We could actually use multiple VBOs (e.g. one for positions, one for texcoords), but one is sufficient
		GLuint mVertexArrayObjectHandle = 0;	// Vertex array object handle (only used if VAOs are actually supported, depending on the platform)
		Format mCurrentFormat = Format::UNDEFINED;

		size_t mNumBufferedVertices = 0;
		size_t mNumVertexAttributes = 0;
		size_t mFloatsPerVertex = 0;
	};
}

#endif
