/*
*	rmx Library
*	Copyright (C) 2008-2025 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*
*	Shader
*		Support for GLSL shaders.
*/

#pragma once

#ifdef RMX_WITH_OPENGL_SUPPORT

#include <functional>


// Shader class
class API_EXPORT Shader
{
public:
	enum class ShaderType
	{
		FRAGMENT,
		VERTEX
	};

	enum class BlendMode
	{
		UNDEFINED = -1,
		OPAQUE,
		ALPHA,
		ADD
	};

	static inline std::function<void(String&, ShaderType)> mShaderSourcePostProcessCallback;
	static inline std::function<bool(BlendMode)> mShaderApplyBlendModeCallback;				// Internal application of blend function will only be done if this is not set, or returns false

public:
	static void unbindShader();

public:
	Shader();
	~Shader();

	bool compile(const String& vsSource, const String& fsSource, const std::map<int, String>* vertexAttribMap = nullptr);

	inline const String& getVertexSource() const	{ return mVertexSource; }
	inline const String& getFragmentSource() const	{ return mFragmentSource; }
	inline const String& getCompileLog() const		{ return mCompileLog; }

	inline BlendMode getBlendMode() const			{ return mBlendMode; }
	inline void setBlendMode(BlendMode mode)		{ mBlendMode = mode; }

	inline GLuint getProgramHandle() const			{ return mProgram; }

	GLuint getUniformLocation(const char* name) const;
	GLuint getAttribLocation(const char* name) const;

	void setParam(GLuint loc, int param);
	void setParam(GLuint loc, const Vec2i& param);
	void setParam(GLuint loc, const Vec3i& param);
	void setParam(GLuint loc, const Vec4i& param);
	void setParam(GLuint loc, const Recti& param);
	void setParam(GLuint loc, float param);
	void setParam(GLuint loc, const Vec2f& param);
	void setParam(GLuint loc, const Vec3f& param);
	void setParam(GLuint loc, const Vec4f& param);
	void setParam(GLuint loc, const Rectf& param);

	void setMatrix(GLuint loc, const Mat3f& matrix);
	void setMatrix(GLuint loc, const Mat4f& matrix);

	inline void resetTextureCount()  { mTextureCount = 0; }
	void setTexture(GLuint loc, GLuint handle, GLenum target);
	void setTexture(GLuint loc, const Texture& texture);

	inline void setParam(const char* name, int param)			{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Vec2i& param)	{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Vec3i& param)	{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Vec4i& param)	{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Recti& param)	{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, float param)			{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Vec2f& param)	{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Vec3f& param)	{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Vec4f& param)	{ setParam(getUniformLocation(name), param); }
	inline void setParam(const char* name, const Rectf& param)	{ setParam(getUniformLocation(name), param); }

	inline void setMatrix(const char* name, const Mat3f& matrix)			{ setMatrix(getUniformLocation(name), matrix); }
	inline void setMatrix(const char* name, const Mat4f& matrix)			{ setMatrix(getUniformLocation(name), matrix); }

	inline void setTexture(const char* name, GLuint handle, GLenum target)	{ setTexture(getUniformLocation(name), handle, target); }
	inline void setTexture(const char* name, const Texture& texture)		{ setTexture(getUniformLocation(name), texture); }

	void bind();
	void unbind();

	bool load(const String& filename, const String& techname = String(), const String& additionalDefines = String());
	bool load(const std::vector<uint8>& content, const String& techname = String(), const String& additionalDefines = String());

private:
	bool compileShader(GLenum shaderType, GLuint& shaderHandle, const String& source);
	bool linkProgram(const std::map<int, String>* vertexAttribMap = nullptr);

private:
	GLuint mVertexShader = 0;
	GLuint mFragmentShader = 0;
	GLuint mProgram = 0;
	BlendMode mBlendMode = BlendMode::UNDEFINED;
	int mTextureCount = 0;

	String mVertexSource;
	String mFragmentSource;
	String mCompileLog;
};


// ShaderEffect class
class API_EXPORT ShaderEffect
{
public:
	ShaderEffect();
	~ShaderEffect();

	bool load(const String& filename);
	bool load(const std::vector<uint8>& content);
	bool loadFromString(const String& content);
	bool getShader(Shader& shader, int index = 0, const String& additionalDefines = String());
	bool getShader(Shader& shader, const String& name, const String& additionalDefines = String());

private:
	struct PartStruct
	{
		String mTitle;
		String mContent;
	};

	struct TechniqueStruct
	{
		String mName;
		std::vector<String> mVertexShaderParts;
		std::vector<String> mFragmentShaderParts;
		std::vector<String> mDefines;
		std::map<int, String> mVertexAttribMap;
		Shader::BlendMode mBlendMode = Shader::BlendMode::UNDEFINED;
	};

private:
	TechniqueStruct* findTechniqueByName(const String& name);
	bool getShaderInternal(Shader& shader, TechniqueStruct& tech, const String& additionalDefines);
	void readParts(const String& source);
	void parseTechniques(String& source);
	PartStruct* findPartByName(const String& name);
	void buildSourceFromParts(String& source, const std::vector<String>& parts, const String& definitions);
	void preprocessSource(String& source, Shader::ShaderType shaderType);

private:
	std::vector<PartStruct> mParts;
	std::vector<TechniqueStruct> mTechniques;
	String mIncludeDir;
};

#endif
