/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/compiler/Compiler.h"
#include "lemon/compiler/Utility.h"
#include "lemon/compiler/backend/FunctionCompiler.h"
#include "lemon/compiler/frontend/CompilerFrontend.h"
#include "lemon/program/Module.h"
#include "lemon/translator/Translator.h"


namespace lemon
{

	Compiler::Compiler(Module& module, GlobalsLookup& globalsLookup, const CompileOptions& compileOptions) :
		mModule(module),
		mGlobalsLookup(globalsLookup),
		mCompileOptions(compileOptions),
		mTokenProcessing(globalsLookup, compileOptions),
		mPreprocessor(compileOptions, mTokenProcessing)
	{
	}

	Compiler::~Compiler()
	{
		// Free some memory again, by shrinking at least the largest pools
		genericmanager::Manager<Node>::shrinkAllPools();
		genericmanager::Manager<Token>::shrinkAllPools();
	}

	bool Compiler::loadScript(const std::wstring& path)
	{
		mErrors.clear();
		mModule.startCompiling(mGlobalsLookup);

		// Read input file(s)
		std::vector<std::string_view> inputLines;
		bool compileSuccess = loadCodeLines(inputLines, path);
		if (!compileSuccess)
			return false;

		// Compile
		compileSuccess = compileLines(inputLines);
		return compileSuccess;
	}

	bool Compiler::loadCodeLines(std::vector<std::string_view>& outLines, const std::wstring& path)
	{
		// Split into base path and file name
		WString basepath;
		WString filename = path;
		int pos = filename.findChars(L"/\\", filename.length() - 1, -1);
		if (pos > 0)
		{
			basepath = filename.getSubString(0, pos);
			basepath.add('/');
			filename.makeSubString(pos + 1, -1);
		}

		mScriptFiles.clear();
		mScriptFiles.reserve(0x200);

		// Recursively load script files
		std::unordered_set<uint64> includedPathHashes;
		if (!loadScriptInternal(*basepath, *filename, outLines, includedPathHashes))
			return false;

		if (!mCompileOptions.mOutputCombinedSource.empty())
		{
			String output;
			for (const std::string_view& line : outLines)
				output << line << "\r\n";
			output.saveFile(mCompileOptions.mOutputCombinedSource);
		}

		return true;
	}

	bool Compiler::compileLines(const std::vector<std::string_view>& lines)
	{
		try
		{
			BlockNode rootNode;
			std::vector<FunctionNode*> functionNodes;

			// Frontend part: Convert input text lines into a syntax tree like structure (built of nodes and tokens)
			CompilerFrontend frontend(mModule, mGlobalsLookup, mCompileOptions, mLineNumberTranslation, functionNodes);
			frontend.runCompilerFrontend(rootNode, lines);

			// Optional translation
			if (!mCompileOptions.mOutputTranslatedSource.empty())
			{
				Translator::translateToCppAndSave(mCompileOptions.mOutputTranslatedSource, rootNode);
			}

			// Backend part: Compile functions' syntax tree structure into opcodes
			runCompilerBackend(functionNodes);

			// Success
			return true;
		}
		catch (const CompilerException& e)
		{
			const auto& translated = mLineNumberTranslation.translateLineNumber(e.mError.mLineNumber);
			ErrorMessage& error = vectorAdd(mErrors);
			error.mMessage = e.what();
			error.mFilename = translated.mSourceFileInfo->mFilename;
			error.mError = e.mError;
			error.mError.mLineNumber = translated.mLineNumber + 1;	// Add one because line numbers always start at 1 for user display
		}

		return false;
	}

	bool Compiler::loadScriptInternal(const std::wstring& basepath, const std::wstring& filename, std::vector<std::string_view>& outLines, std::unordered_set<uint64>& includedPathHashes)
	{
		const std::wstring filepath = basepath + filename;
		const uint64 pathHash = rmx::getMurmur2_64(filepath);
		if (includedPathHashes.count(pathHash) > 0)
		{
			// File was already included before, silently ignore the double inclusion
			return true;
		}
		includedPathHashes.insert(pathHash);

		ScriptFile& scriptFile = mScriptFilesPool.createObject();
		mScriptFiles.push_back(&scriptFile);
		scriptFile.mBasePath = basepath;
		scriptFile.mFilename = filename;
		scriptFile.mFirstLine = outLines.size() + 1;

		if (!scriptFile.mContent.loadFile(filepath))
		{
			ErrorMessage& error = vectorAdd(mErrors);
			error.mMessage = "Failed to load script file '" + WString(filename).toStdString() + "' at '" + WString(basepath).toStdString() + "'";
			return false;
		}

		// Register source file at module
		const SourceFileInfo& sourceFileInfo = mModule.addSourceFileInfo(basepath, filename);

		// Update line number translation
		mLineNumberTranslation.push((uint32)outLines.size() + 1, sourceFileInfo, 0);

		// Split input into lines
		std::vector<std::string_view> fileLines;
		fileLines.reserve((size_t)scriptFile.mContent.length() / 20);	// Rough estimate: At least 20 characters per average line
		{
			int pos = 0;
			while (pos < scriptFile.mContent.length())
			{
				const int start = pos;
				size_t length;
				pos = scriptFile.mContent.getLine(length, start);
				fileLines.emplace_back(&scriptFile.mContent[start], length);
			}
		}

		// Your turn, preprocessor
		try
		{
			mPreprocessor.mPreprocessorDefinitions = &mGlobalsLookup.mPreprocessorDefinitions;
			mPreprocessor.processLines(fileLines);
			mModule.registerNewPreprocessorDefinitions(mGlobalsLookup.mPreprocessorDefinitions);
		}
		catch (const CompilerException& e)
		{
			ErrorMessage& error = vectorAdd(mErrors);
			error.mMessage = e.what();
			error.mFilename = filename;
			error.mError = e.mError;
			return false;
		}

		// Build output
		for (uint32 fileLineIndex = 0; fileLineIndex < (uint32)fileLines.size(); ++fileLineIndex)
		{
			const std::string_view line = fileLines[fileLineIndex];

			// Resolve include
			if (line.rfind("include ", 0) == 0)
			{
				String includeFilename = line.substr(8);
				includeFilename.makeSubString(0, includeFilename.findChar(' ', 0, +1));

				// Use only forward slashes
				includeFilename.replace('\\', '/');

				// Split into base path and file name
				String includeBasepath;
				int pos = includeFilename.findChar('/', includeFilename.length()-1, -1);
				if (pos > 0)
				{
					includeBasepath = includeFilename.getSubString(0, pos);
					includeBasepath.add('/');
					includeFilename.makeSubString(pos+1, -1);
				}

				// Wildcard support
				if (includeFilename == "?")
				{
					std::vector<rmx::FileIO::FileEntry> fileEntries;
					fileEntries.reserve(8);
					FTX::FileSystem->listFilesByMask(basepath + *includeBasepath.toWString() + L"*.lemon", false, fileEntries);
					for (const rmx::FileIO::FileEntry& fileEntry : fileEntries)
					{
						if (!loadScriptInternal(basepath + *includeBasepath.toWString(), fileEntry.mFilename, outLines, includedPathHashes))
							return false;
					}
				}
				else
				{
					if (!loadScriptInternal(basepath + *includeBasepath.toWString(), *(includeFilename + ".lemon").toWString(), outLines, includedPathHashes))
						return false;
				}

				// Update line number translation
				//  -> Back to this file
				const uint32 currentLineNumber = (uint32)outLines.size() + 1;
				mLineNumberTranslation.push(currentLineNumber, sourceFileInfo, fileLineIndex);
			}
			else
			{
				outLines.emplace_back(std::move(fileLines[fileLineIndex]));
			}
		}

		return true;
	}

	void Compiler::runCompilerBackend(std::vector<FunctionNode*>& functionNodes)
	{
		// Backend part: Compile function contents into opcodes
		for (FunctionNode* node : functionNodes)
		{
			FunctionCompiler functionCompiler(*node->mFunction, mCompileOptions, mGlobalsLookup);
			functionCompiler.processParameters();
			functionCompiler.buildOpcodesForFunction(*node->mContent);
		}

	#if 0
		// Just for debugging: Build compiled hash
		{
			uint64 hash = rmx::startFNV1a_64();
			for (ScriptFunction* function : mModule.getScriptFunctions())
			{
				hash = function->addToCompiledHash(hash);
			}
			mModule.setCompiledCodeHash(hash);
			RMX_LOG_INFO("Hash for module '" << mModule.getModuleName() << "' = " << rmx::hexString(hash, 16));
		}
	#endif
	#if 0
		// Also for debugging: Text output of opcodes
		{
			String output;
			for (ScriptFunction* function : mModule.getScriptFunctions())
			{
				output << function->getName().getString() << ":\r\n";
				for (const Opcode& opcode : function->mOpcodes)
				{
					String typeString = Opcode::GetTypeString(opcode.mType);
					switch (opcode.mDataType)
					{
						case BaseType::VOID:	 break;
						case BaseType::UINT_8:	 typeString << ".u8";   break;
						case BaseType::UINT_16:	 typeString << ".u16";  break;
						case BaseType::UINT_32:	 typeString << ".u32";  break;
						case BaseType::UINT_64:	 typeString << ".u64";  break;
						case BaseType::INT_8:	 typeString << ".s8";   break;
						case BaseType::INT_16:	 typeString << ".s16";  break;
						case BaseType::INT_32:	 typeString << ".s32";  break;
						case BaseType::INT_64:	 typeString << ".s64";  break;
						default:
							typeString << "(" << rmx::hexString((uint8)opcode.mDataType, 2) << ")";
							break;
					}
					output << "\t" << typeString << " ";

					if (opcode.mParameter != 0)
					{
						output.add(' ', 22 - typeString.length());
						output << rmx::hexString(opcode.mParameter, 16);
					}
					output << "\r\n";
				}
				output << "\r\n";
			}
			output.saveFile(L"function_opcodes.txt");
		}
	#endif
	}

}
