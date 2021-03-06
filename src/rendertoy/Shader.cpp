#pragma once
#include "Shader.h"
#include "StringUtil.h"
#include "FileUtil.h"
#include <glad/glad.h>
#include <unordered_set>
#include <fstream>

vector<char> loadShaderSource(const std::string& path, const char* preprocessorOptions)
{
	/*std::string preprocessedFile = path + ".preprocessed";

	//if (getFileModifiedTime(preprocessedFile) < getFileModifiedTime(path))
	{
	std::string cmd = "cpp -P " + path + " -o " + preprocessedFile + " " + preprocessorOptions;
	system(cmd.c_str());
	}

	std::vector<char> result = loadTextFileZ(preprocessedFile.c_str());*/

	std::vector<char> result = loadTextFileZ(path.c_str());
	std::string prefix = "#version 440\n#line 0\n";

	result.insert(result.begin(), prefix.begin(), prefix.end());
	return result;
}

bool parseTextureSizeAnnotations(const ParamAnnotation& annotation, TextureSize *const res)
{
	if (annotation.has("relativeTo")) {
		res->scaleRelativeTo = annotation.get("relativeTo", "");
		res->useRelativeScale = true;

		if (annotation.has("scale")) {
			sscanf(annotation.get("scale", ""), "%f %f", &res->relativeScale.x, &res->relativeScale.y);
		}

		return true;
	}
	else if (annotation.has("size")) {
		sscanf(annotation.get("size", ""), "%u %u", &res->resolution.x, &res->resolution.y);
		res->useRelativeScale = false;
		return true;
	}

	return false;
}

ShaderParamValue ShaderParamRefl::defaultValue() const
{
	ShaderParamValue res;

	if (ShaderParamType::Float == type) {
		res.floatValue = annotation.get("default", 0.0f);
	}
	else if (ShaderParamType::Float2 == type) {
		res.float2Value = vec2(annotation.get("default", 0.0f));
	}
	else if (ShaderParamType::Float3 == type) {
		res.float3Value = vec3(annotation.get("default", annotation.has("color") ? 1.0f : 0.0f));
	}
	else if (ShaderParamType::Float4 == type) {
		res.float4Value = vec4(annotation.get("default", annotation.has("color") ? 1.0f : 0.0f));
	}
	else if (ShaderParamType::Int == type) {
		res.intValue = annotation.get("default", 0);
	}
	else if (ShaderParamType::Int2 == type) {
		res.int2Value = ivec2(annotation.get("default", 0));
	}
	else if (ShaderParamType::Int3 == type) {
		res.int3Value = ivec3(annotation.get("default", 0));
	}
	else if (ShaderParamType::Int4 == type) {
		res.int4Value = ivec4(annotation.get("default", 0));
	}
	else if (ShaderParamType::Sampler2d == type) {
		if (annotation.has("input")) {
			res.textureValue.source = TextureDesc::Source::Input;
		}
		else 
		{
			if (annotation.has("default")) {
				res.textureValue.path = annotation.get("default", "");
			}
			res.textureValue.source = TextureDesc::Source::Load;
		}
	}
	else if (ShaderParamType::Image2d == type) {
		if (annotation.has("input")) {
			res.textureValue.source = TextureDesc::Source::Input;
		}
		else if (annotation.has("default")) {
			res.textureValue.path = annotation.get("default", "");
			res.textureValue.source = TextureDesc::Source::Load;
		}
		else {
			res.textureValue.source = TextureDesc::Source::Create;
			parseTextureSizeAnnotations(annotation, &res.textureValue.size);

			if (annotation.has("format")) {
				parseTextureFormat(annotation.get("format", ""), &res.textureValue.createFormat);
			}
		}
	}

	return res;
}

static ShaderParamType parseShaderType(GLenum type, GLint size)
{
	static std::unordered_map<GLenum, ShaderParamType> tmap = {
		{ GL_FLOAT, ShaderParamType::Float },
		{ GL_FLOAT_VEC2, ShaderParamType::Float2 },
		{ GL_FLOAT_VEC3, ShaderParamType::Float3 },
		{ GL_FLOAT_VEC4, ShaderParamType::Float4 },
		{ GL_INT, ShaderParamType::Int },
		{ GL_INT_VEC2, ShaderParamType::Int2 },
		{ GL_INT_VEC3, ShaderParamType::Int3 },
		{ GL_INT_VEC4, ShaderParamType::Int4 },
		{ GL_SAMPLER_2D, ShaderParamType::Sampler2d },
		{ GL_IMAGE_2D, ShaderParamType::Image2d },
		{ GL_INT_IMAGE_2D, ShaderParamType::Image2d },
		{ GL_UNSIGNED_INT_IMAGE_2D, ShaderParamType::Image2d },
	};

	auto it = tmap.find(type);
	if (it != tmap.end()) {
		return it->second;
	}

	return ShaderParamType::Unknown;
}

static bool parseParenthesizedExpression(const char*& c, const char* aend)
{
	assert(*c == '(');
	++c;
	int level = 1;
	while (c < aend) {
		if ('(' == *c) ++level;
		else if (')' == *c) {
			--level;
			if (0 == level) return true;
		}
		++c;
	}

	return false;
}

static bool parseAnnotation(const char* abegin, const char* aend, ParamAnnotation *const annot)
{
	const char* c = abegin;
	auto skipWhite = [&]() {
		while (c < aend && isspace(*c)) ++c;
		return c < aend;
	};

	while (c < aend) {
		skipWhite();

		if (isalnum(*c)) {
			const char* tbegin = c;
			while (isalnum(*c)) {
				++c;
			}
			const char* tend = c;

			skipWhite();

			std::string annotValue;
			if (*c == '(')
			{
				const char* const exprBegin = c;
				if (!parseParenthesizedExpression(c, aend)) {
					return false;
				}

				const char* const exprEnd = c;
				assert('(' == *exprBegin);
				assert(')' == *exprEnd);
				annotValue = std::string(exprBegin + 1, exprEnd);
				++c;
			}

			annot->items[std::string(tbegin, tend)] = annotValue;
		} else {
			return false;
		}
	}

	return true;
}

static std::string getInfoLog(
	GLuint object,
	PFNGLGETSHADERIVPROC glGet__iv,
	PFNGLGETSHADERINFOLOGPROC glGet__InfoLog
)
{
	GLint log_length;
	char *log;

	glGet__iv(object, GL_INFO_LOG_LENGTH, &log_length);
	log = (char*)malloc(log_length);
	glGet__InfoLog(object, log_length, NULL, log);
	std::string result = log;
	free(log);

	return result;
}

// Returns 0 on failure
static GLuint makeShader(GLenum shaderType, const vector<char>& source, std::string *const errorLog)
{
	GLuint handle = glCreateShader(shaderType);
	if (!handle) {
		*errorLog = "glCreateShader failed";
		return 0;
	}

	GLint sourceLength = (GLint)source.size();
	const GLchar* sources[1] = { source.data() };
	glShaderSource(handle, 1, sources, &sourceLength);

	glCompileShader(handle);
	{
		GLint shader_ok;
		glGetShaderiv(handle, GL_COMPILE_STATUS, &shader_ok);

		if (!shader_ok) {
			*errorLog = getInfoLog(handle, glGetShaderiv, glGetShaderInfoLog);
			glDeleteShader(handle);
			return 0;
		}
	}

	return handle;
}

static GLuint makeProgram(GLuint computeShader, std::string *const errorLog)
{
	GLint program_ok;

	GLuint program = glCreateProgram();
	glAttachShader(program, computeShader);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &program_ok);

	if (!program_ok) {
		*errorLog = getInfoLog(program, glGetProgramiv, glGetProgramInfoLog);
		glDeleteProgram(program);
		return 0;
	}

	return program;
}


void ComputeShader::reflectParams(const ComputeShader::AnnotationMap& annotations)
{
	{
		GLint activeUniformCount = 0;
		glGetProgramiv(m_programHandle, GL_ACTIVE_UNIFORMS, &activeUniformCount);
		printf("Active uniform count: %d\n", activeUniformCount);

		m_params.resize(activeUniformCount);

		char name[1024];
		for (GLint loc = 0; loc < activeUniformCount; ++loc) {
			GLsizei nameLength = 0;
			GLenum typeGl;
			GLint size;
			glGetActiveUniform(m_programHandle, loc, sizeof(name), &nameLength, &size, &typeGl, name);

			ShaderParamBindingRefl& param = m_params[loc];
			param.location = loc;
			param.name = name;
			param.type = parseShaderType(typeGl, size);

			auto it = annotations.find(name);
			if (it != annotations.end()) {
				param.annotation = it->second;
			}
		}
	}

	{
		GLint shaderStorageBlockCount = 0;
		glGetProgramInterfaceiv(m_programHandle, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &shaderStorageBlockCount);
		printf("shaderStorageBlockCount = %d\n", shaderStorageBlockCount);

		for (GLint bufferIdx = 0; bufferIdx < shaderStorageBlockCount; ++bufferIdx) {
			char name[1024];
			GLsizei nameLength = 0, propArrayLength;
			glGetProgramResourceName(m_programHandle, GL_SHADER_STORAGE_BLOCK, bufferIdx, sizeof(name), &nameLength, name);

			struct {
				GLint activeVariableCount = 0;
				GLint bufferDataSize = 0;
				GLint bufferBiniding;
			} props;

			{
				GLenum propNames[] = {
					GL_NUM_ACTIVE_VARIABLES,
					GL_BUFFER_DATA_SIZE,
					GL_BUFFER_BINDING,
				};
				const auto propCount = sizeof(propNames) / sizeof(*propNames);
				glGetProgramResourceiv(m_programHandle, GL_SHADER_STORAGE_BLOCK, bufferIdx, propCount, propNames, propCount, &propArrayLength, (GLint*)&props);
			}

			vector<GLint> variableIndices(props.activeVariableCount);
			{
				GLenum propName = GL_ACTIVE_VARIABLES;
				glGetProgramResourceiv(m_programHandle, GL_SHADER_STORAGE_BLOCK, bufferIdx, 1, &propName, variableIndices.size(), &propArrayLength, variableIndices.data());
			}

			printf("Buffer %s active vars: %d data size: %d\n", name, props.activeVariableCount, props.bufferDataSize);

			BufferSize bufferSize;
			bufferSize.baseSizeBytes = props.bufferDataSize;

			for (GLint varIdx = 0; varIdx < props.activeVariableCount; ++varIdx) {
				struct {
					GLint arraySize = 0;
					GLint arrayStride = 0;
					GLint offset = 0;
				} varProps;

				{
					GLenum propNames[] = {
						GL_ARRAY_SIZE,
						GL_ARRAY_STRIDE,
						GL_OFFSET,
					};
					const auto propCount = sizeof(propNames) / sizeof(*propNames);
					glGetProgramResourceiv(m_programHandle, GL_BUFFER_VARIABLE, variableIndices[varIdx], propCount, propNames, propCount, &propArrayLength, (GLint*)&varProps);
				}

				char varName[1024];
				glGetProgramResourceName(m_programHandle, GL_BUFFER_VARIABLE, variableIndices[varIdx], sizeof(varName), &nameLength, varName);

				printf("    variable %s array size %d stride %d\n", varName, varProps.arraySize, varProps.arrayStride);

				if (0 == varProps.arraySize) {
					// Unsized array, last in the buffer. With the array sized zero, the buffer
					// must be large enough to hold everything up to varProps.offset.
					bufferSize.baseSizeBytes = varProps.offset;
					bufferSize.tailArrayStrideBytes = varProps.arrayStride;
				}
			}

			m_params.push_back(ShaderParamBindingRefl());
			ShaderParamBindingRefl& param = m_params.back();
			param.location = props.bufferBiniding;
			param.name = name;
			param.type = ShaderParamType::Buffer;
			param.bufferSize = bufferSize;
		}
	}

	std::unordered_set<std::string> textureParamNames;
	for (auto& p : m_params) {
		if (p.type == ShaderParamType::Sampler2d || p.type == ShaderParamType::Image2d) {
			textureParamNames.insert(p.name);
		}
	}

	m_params.erase(
		std::remove_if(m_params.begin(), m_params.end(), [&](const ShaderParamBindingRefl& p) {
			if (p.type == ShaderParamType::Float2 || p.type == ShaderParamType::Float4 || p.type == ShaderParamType::Int2) {
				if (ends_with(p.name, "_size")) {
					std::string texName = p.name.substr(0, p.name.length() - 5);
					return textureParamNames.find(texName) != textureParamNames.end();
				}
			}

			return false;
		}),
		m_params.end()
	);
}

void ComputeShader::initializeDefaultDispatchSize(const ComputeShader::AnnotationMap& annotations)
{
	m_hasDefaultDispatchSize = false;

	auto it = annotations.find("in");
	if (it != annotations.end()) {
		if (parseTextureSizeAnnotations(it->second, &m_defaultDispatchSize)) {
			m_hasDefaultDispatchSize = true;
		}
	}
}

std::unordered_map<std::string, ParamAnnotation> ComputeShader::parseAnnotations(const vector<char>& source)
{
	std::unordered_map<std::string, ParamAnnotation> result;

	auto processLine = [&](const char* lbegin, const char* lend) {
		while (lend > lbegin && lend[-1] == '\r') --lend;

		const char* tagBegin = "//@";
		const char* tagEnd = tagBegin + strlen(tagBegin);
		const char* annotBegin = std::search(lbegin, lend, tagBegin, tagEnd);
		if (annotBegin == lend) return;

		const char* identEnd = annotBegin;

		// Skip the tag from the annotation
		annotBegin += tagEnd - tagBegin;

		// Find the identifier
		while (identEnd > lbegin && *identEnd != ';') --identEnd;
		if (lbegin == identEnd) return;

		while (identEnd > lbegin && isspace(*identEnd)) --identEnd;
		if (lbegin == identEnd) return;

		const char* identBegin = identEnd - 1;
		while (identBegin > lbegin && (isalnum(*identBegin) || '_' == *identBegin)) --identBegin;
		++identBegin;

		if (identEnd - identBegin <= 0) {
			return;
		}

		std::string paramName = std::string(identBegin, identEnd);
		//auto param = std::find_if(params.begin(), params.end(), [&paramName](ShaderParam& it) { return it.name == paramName; });
		//if (param != params.end())
		{
			ParamAnnotation annot;
			if (parseAnnotation(annotBegin, lend, &annot)) {
				result[paramName] = annot;
			}
		}

		//printf("Ident: '%.*s' annotation: '%.*s'\n", int(identEnd - identBegin), identBegin, int(lend - annotBegin), annotBegin);
	};

	const char* lbegin = source.data();
	const char *const fend = source.data() + source.size();
	for (const char* lend = source.data(); lend != fend; ++lend) {
		if ('\n' == *lend) {
			processLine(lbegin, lend);
			lbegin = lend + 1;
		}
	}

	if (fend - lbegin > 1) {
		processLine(lbegin, fend);
	}

	return result;
}

void ComputeShader::updateErrorLogFile()
{
	if (m_errorLog.length() > 0) {
		std::ofstream(m_sourceFile + ".errors").write(m_errorLog.data(), m_errorLog.size());
	}
	else {
		remove(fs::path(m_sourceFile + ".errors"));
	}
}

bool ComputeShader::reload()
{
	m_errorLog.clear();

	vector<char> source = loadShaderSource(m_sourceFile, "");
	GLuint sHandle = makeShader(GL_COMPUTE_SHADER, source, &m_errorLog);
	if (!sHandle) {
		updateErrorLogFile();
		return false;
	}

	GLuint pHandle = makeProgram(sHandle, &m_errorLog);
	if (!pHandle) {
		updateErrorLogFile();
		return false;
	}

	m_programHandle = pHandle;
	m_csHandle = sHandle;
	++versionId;

	updateErrorLogFile();

	auto annotations = parseAnnotations(source);
	reflectParams(annotations);
	initializeDefaultDispatchSize(annotations);

	return true;
}
