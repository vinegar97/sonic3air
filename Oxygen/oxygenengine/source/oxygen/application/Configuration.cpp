/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2022 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/base/PlatformFunctions.h"
#include "oxygen/helper/JsonHelper.h"

#include <lemon/translator/SourceCodeWriter.h>


namespace
{
	static const std::string InputMappingKeys[12] = { "Up", "Down", "Left", "Right", "A", "B", "X", "Y", "Start", "Back" };

	void tryParseWindowSize(String string, Vec2i& result)
	{
		std::vector<String> resolution;
		string.split(resolution, 'x');
		if (resolution.size() >= 2)
		{
			result.x = resolution[0].parseInt();
			result.y = resolution[1].parseInt();
		}
	}

	void readInputDevices(const Json::Value& rootJson, std::vector<InputConfig::DeviceDefinition>& inputDeviceDefinitions)
	{
		// Input devices
		const auto& devicesJson = rootJson["InputDevices"];
		if (!devicesJson.isObject())
			return;

		for (auto it = devicesJson.begin(); it != devicesJson.end(); ++it)
		{
			const std::string key = it.key().asString();

			// Check for overwrite
			InputConfig::DeviceDefinition* inputDeviceDefinition = nullptr;
			for (InputConfig::DeviceDefinition& definition : inputDeviceDefinitions)
			{
				if (definition.mIdentifier == key)
				{
					inputDeviceDefinition = &definition;
					break;
				}
			}

			if (nullptr == inputDeviceDefinition)
			{
				// Definition does not exist yet, this must be an unknown gamepad
				inputDeviceDefinition = &vectorAdd(inputDeviceDefinitions);
				inputDeviceDefinition->mIdentifier = it.key().asString();
				inputDeviceDefinition->mDeviceType = InputConfig::DeviceType::GAMEPAD;
			}

			// Collect device names
			const Json::Value deviceNames = (*it)["DeviceNames"];
			if (deviceNames.isArray())
			{
				for (Json::ArrayIndex i = 0; i < deviceNames.size(); ++i)
				{
					const std::string name = deviceNames[i].asString();
					if (!name.empty())
					{
						String str(name);
						str.lowerCase();
						const uint64 hash = rmx::getMurmur2_64(str);
						inputDeviceDefinition->mDeviceNames[hash] = *str;
					}
				}
			}

			// Read mappings
			std::vector<InputConfig::Assignment> newAssignments;
			for (size_t buttonIndex = 0; buttonIndex < (size_t)InputConfig::DeviceDefinition::Button::_NUM; ++buttonIndex)
			{
				const Json::Value& mappingJson = (*it)[InputMappingKeys[buttonIndex]];
				newAssignments.clear();
				if (mappingJson.isArray())
				{
					for (Json::ArrayIndex k = 0; k < mappingJson.size(); ++k)
					{
						InputConfig::Assignment assignment;
						if (InputConfig::Assignment::setFromMappingString(assignment, mappingJson[k].asString(), inputDeviceDefinition->mDeviceType))
						{
							newAssignments.push_back(assignment);
						}
					}
				}
				else if (mappingJson.isString())
				{
					std::vector<String> parts;
					String(mappingJson.asCString()).split(parts, ',');
					for (String& part : parts)
					{
						// Trim whitespace
						part.trimWhitespace();
						if (part.length() > 0)
						{
							InputConfig::Assignment assignment;
							if (InputConfig::Assignment::setFromMappingString(assignment, part.toStdString(), inputDeviceDefinition->mDeviceType))
							{
								newAssignments.push_back(assignment);
							}
						}
					}
				}

				// Set assignments, and allow for duplicate assignments (i.e. having a real button mapped to multiple controls) in this case
				//  -> This is not allowed when using S3AIR's controls setup menu, as it can potentially lead to weird in-game behavior
				//  -> But by manipulating the settings_input.json directly, players can still have duplicate assignments if they want to - e.g. if their controller has only very few buttons
				InputConfig::setAssignments(*inputDeviceDefinition, buttonIndex, newAssignments, false);
			}
		}
	}

	void tryReadRenderMethod(JsonHelper& rootHelper, bool failSafeMode, Configuration::RenderMethod& outRenderMethod, bool& outAutoDetect)
	{
		String renderMethodString;
		{
			std::string renderMethodStdString;
			if (rootHelper.tryReadString("RenderMethod", renderMethodStdString))
			{
				renderMethodString = renderMethodStdString;
				renderMethodString.lowerCase();

				outAutoDetect = (renderMethodString == "auto");
				if (outAutoDetect)
				{
					outRenderMethod = Configuration::RenderMethod::OPENGL_FULL;
				}
			}
		}

		if (failSafeMode)
		{
			outRenderMethod = Configuration::RenderMethod::SOFTWARE;
		}
		else
		{
			if (renderMethodString.startsWith("opengl"))
			{
				outRenderMethod = (renderMethodString.endsWith("soft") | renderMethodString.endsWith("software")) ? Configuration::RenderMethod::OPENGL_SOFT : Configuration::RenderMethod::OPENGL_FULL;
			}
			else if (renderMethodString == "software")
			{
				outRenderMethod = Configuration::RenderMethod::SOFTWARE;
			}

			if (outRenderMethod == Configuration::RenderMethod::UNDEFINED)
			{
				bool useSoftwareRenderer = false;
				if (rootHelper.tryReadBool("UseSoftwareRenderer", useSoftwareRenderer))
				{
					outRenderMethod = useSoftwareRenderer ? Configuration::RenderMethod::OPENGL_SOFT : Configuration::RenderMethod::OPENGL_FULL;
				}
			}
		}
	}

	void loadModSettings(const Json::Value& rootJson, std::map<uint64, Configuration::Mod>& modSettings)
	{
		const auto& modJson = rootJson["ModSettings"];
		if (!modJson.isObject())
			return;

		for (auto it = modJson.begin(); it != modJson.end(); ++it)
		{
			const std::string modName = it.key().asString();
			const uint64 modNameHash = rmx::getMurmur2_64(modName);

			Configuration::Mod& mod = modSettings[modNameHash];
			mod.mModName = modName;

			for (auto it2 = it->begin(); it2 != it->end(); ++it2)
			{
				if (it2->isNumeric())
				{
					const std::string key = it2.key().asString();
					const uint64 keyHash = rmx::getMurmur2_64(key);

					Configuration::Mod::Setting& setting = mod.mSettings[keyHash];
					setting.mIdentifier = key;
					setting.mValue = (uint32)it2->asInt();
				}
			}
		}
	}

	void saveModSettings(Json::Value& rootJson, const std::map<uint64, Configuration::Mod>& modSettings)
	{
		Json::Value modSettingsJson;
		for (const auto& pair : modSettings)
		{
			if (!pair.second.mSettings.empty())
			{
				Json::Value modJson;
				for (const auto& pair2 : pair.second.mSettings)
				{
					modJson[pair2.second.mIdentifier] = pair2.second.mValue;
				}
				modSettingsJson[pair.second.mModName] = modJson;
			}
		}
		rootJson["ModSettings"] = modSettingsJson;
	}
}



Configuration* Configuration::mSingleInstance = nullptr;

Configuration::Configuration()
{
	mSingleInstance = this;

	// Setup default value, just in case (needed e.g. for compiling scripts from source on Android)
	mScriptsDir = L"./scripts/";

#if defined(PLATFORM_WEB)
	// Threading in general is not (afaik) supported by emscripten
	mUseAudioThreading = false;
#endif

}

void Configuration::initialization()
{
	// Setup defaults for input devices
	mInputDeviceDefinitions.reserve(8);
	InputConfig::setupDefaultDeviceDefinitions(mInputDeviceDefinitions);

	preLoadInitialization();
}

bool Configuration::loadConfiguration(const std::wstring& filename)
{
	// Open file
	const Json::Value root = JsonHelper::loadFile(filename);
	const bool loaded = !root.isNull();		// If the config.json was not found, just silently ignore that for now, and return false in the end
	JsonHelper rootHelper(root);

#ifdef PLATFORM_WINDOWS
	// Just for debugging
	bool wait = false;
	if (loaded && rootHelper.tryReadBool("WaitForDebugger", wait) && wait)
	{
		PlatformFunctions::showMessageBox("Waiting for debugger", "Attach debugger now, or don't...");
	}
#endif

	// Define fallback values
	mMainScriptName = L"main.lemon";
	if (mEngineDataPath.empty())
		mEngineDataPath = L"data";
	if (mGameDataPath.empty())
		mGameDataPath = L"data";
	if (mAnalysisDir.empty())
		mAnalysisDir = L"___internal/analysis/";

	// This does not get read from the file, but defined by code
	mPreprocessorDefinitions.clear();
	mPreprocessorDefinitions.setDefinition("STANDALONE");

	// Load project path
	if (rootHelper.tryReadString("LoadProject", mProjectPath))
	{
		mProjectPath += L"/";
	}

	// Load everything shared with settings_global
	loadConfigurationProperties(rootHelper);

	// Call subclass implementation
	const bool success = loadConfigurationInternal(rootHelper);
	return loaded && success;
}

bool Configuration::loadSettings(const std::wstring& filename, SettingsType settingsType)
{
	const int settingsIndex = (int)settingsType;
	mSettingsFilenames[settingsIndex] = filename;

	// Open file
	Json::Value root = JsonHelper::loadFile(filename);
	if (root.isNull())
		return false;
	JsonHelper rootHelper(root);

	if (settingsType == SettingsType::GLOBAL)
	{
		loadConfigurationProperties(rootHelper);
	}

	if (settingsType == SettingsType::INPUT)
	{
		// Input devices
		readInputDevices(root, mInputDeviceDefinitions);
	}
	else
	{
		// Paths
		rootHelper.tryReadString("RomPath", mLastRomPath);

		// General
		if (rootHelper.tryReadBool("FailSafeMode", mFailSafeMode))
		{
			if (mFailSafeMode)
				mUseAudioThreading = false;
		}

		// Graphics
		tryReadRenderMethod(rootHelper, mFailSafeMode, mRenderMethod, mAutoDetectRenderMethod);

		rootHelper.tryReadAsInt("Fullscreen", mWindowMode);
		rootHelper.tryReadInt("DisplayIndex", mDisplayIndex);
		rootHelper.tryReadAsInt("FrameSync", mFrameSync);
		rootHelper.tryReadInt("Upscaling", mUpscaling);
		rootHelper.tryReadInt("Backdrop", mBackdrop);
		rootHelper.tryReadInt("Filtering", mFiltering);
		rootHelper.tryReadInt("Scanlines", mScanlines);
		rootHelper.tryReadInt("BackgroundBlur", mBackgroundBlur);
		rootHelper.tryReadInt("PerformanceDisplay", mPerformanceDisplay);

		// Audio
		rootHelper.tryReadFloat("Volume", mAudioVolume);

		// Input
		rootHelper.tryReadString("PreferredGamepadPlayer1", mPreferredGamepad[0]);
		rootHelper.tryReadString("PreferredGamepadPlayer2", mPreferredGamepad[1]);
		rootHelper.tryReadInt("AutoAssignGamepadPlayerIndex", mAutoAssignGamepadPlayerIndex);

		// Virtual gamepad
		if (!root["VirtualGamepad"].isNull())
		{
			JsonHelper vgHelper(root["VirtualGamepad"]);
			vgHelper.tryReadFloat("Opacity", mVirtualGamepad.mOpacity);
			vgHelper.tryReadInt("DPadPosX", mVirtualGamepad.mDirectionalPadCenter.x);
			vgHelper.tryReadInt("DPadPosY", mVirtualGamepad.mDirectionalPadCenter.y);
			vgHelper.tryReadInt("DPadSize", mVirtualGamepad.mDirectionalPadSize);
			vgHelper.tryReadInt("ButtonsPosX", mVirtualGamepad.mFaceButtonsCenter.x);
			vgHelper.tryReadInt("ButtonsPosY", mVirtualGamepad.mFaceButtonsCenter.y);
			vgHelper.tryReadInt("ButtonsSize", mVirtualGamepad.mFaceButtonsSize);
			vgHelper.tryReadInt("StartPosX", mVirtualGamepad.mStartButtonCenter.x);
			vgHelper.tryReadInt("StartPosY", mVirtualGamepad.mStartButtonCenter.y);
		}

		// Mod settings
		loadModSettings(root, mModSettings);
	}

	// Call subclass implementation
	const bool success = loadSettingsInternal(rootHelper, settingsType);

	// Cleanup?
	{
		bool retainOldEntries = true;
		switch (settingsType)
		{
			case SettingsType::STANDARD:
			{
				bool performCleanup = false;
				if (rootHelper.tryReadBool("CleanupSettings", performCleanup))
				{
					retainOldEntries = !performCleanup;
				}
				break;
			}

			case SettingsType::INPUT:
			{
				retainOldEntries = false;
				break;
			}

			case SettingsType::GLOBAL:
			{
				retainOldEntries = true;
				break;
			}
		}

		if (retainOldEntries)
		{
			// Backup old settings values
			//  -> Especially the unknown keys, for easy forward compatibility with future additions
			mSettingsJsons[settingsIndex].swap(root);
		}
		else
		{
			mSettingsJsons[settingsIndex] = Json::Value();
		}
	}

	return success;
}

void Configuration::saveSettings()
{
	// Do not save if settings are set to read-only
	if (mSettingsReadOnly)
		return;

	// Save standard settings
	int settingsIndex = (int)SettingsType::STANDARD;
	if (!mSettingsFilenames[settingsIndex].empty())
	{
		Json::Value root = mSettingsJsons[settingsIndex];
		root["CleanupSettings"] = 0;

		// Paths
		root["RomPath"] = WString(mLastRomPath).toStdString();

		// General
		root["RenderMethod"] = mAutoDetectRenderMethod ? "auto" :
							   (mRenderMethod == RenderMethod::OPENGL_FULL) ? "opengl-full" :
							   (mRenderMethod == RenderMethod::OPENGL_SOFT) ? "opengl-soft" : "software";
		root["FailSafeMode"] = mFailSafeMode;
		root["PlatformFlags"] = mPlatformFlags;

		// Graphics
		root["Fullscreen"] = (int)mWindowMode;
		root["DisplayIndex"] = mDisplayIndex;
		root["FrameSync"] = (int)mFrameSync;
		root["Upscaling"] = mUpscaling;
		root["Backdrop"] = mBackdrop;
		root["Filtering"] = mFiltering;
		root["Scanlines"] = mScanlines;
		root["BackgroundBlur"] = mBackgroundBlur;
		root["PerformanceDisplay"] = mPerformanceDisplay;

		// Audio
		root["Volume"] = mAudioVolume;

		// Input
		root["PreferredGamepadPlayer1"] = mPreferredGamepad[0];
		root["PreferredGamepadPlayer2"] = mPreferredGamepad[1];
		root["AutoAssignGamepadPlayerIndex"] = mAutoAssignGamepadPlayerIndex;

		// Virtual gamepad
		{
			Json::Value vg = root["VirtualGamepad"];
			vg["Opacity"] = mVirtualGamepad.mOpacity;
			vg["DPadPosX"] = mVirtualGamepad.mDirectionalPadCenter.x;
			vg["DPadPosY"] = mVirtualGamepad.mDirectionalPadCenter.y;
			vg["DPadSize"] = mVirtualGamepad.mDirectionalPadSize;
			vg["ButtonsPosX"] = mVirtualGamepad.mFaceButtonsCenter.x;
			vg["ButtonsPosY"] = mVirtualGamepad.mFaceButtonsCenter.y;
			vg["ButtonsSize"] = mVirtualGamepad.mFaceButtonsSize;
			vg["StartPosX"] = mVirtualGamepad.mStartButtonCenter.x;
			vg["StartPosY"] = mVirtualGamepad.mStartButtonCenter.y;
			root["VirtualGamepad"] = vg;
		}

		// Mod settings
		saveModSettings(root, mModSettings);

		// Call subclass implementation
		saveSettingsInternal(root, SettingsType::STANDARD);

		// Save file
		JsonHelper::saveFile(mSettingsFilenames[settingsIndex], root);
	}

	// Save global settings
	settingsIndex = (int)SettingsType::GLOBAL;
	if (!mSettingsFilenames[settingsIndex].empty())
	{
		Json::Value root = mSettingsJsons[settingsIndex];
		if (!root.isNull())		// Only overwrite if it existed before already
		{
			// Overwrite only certain properties, namely those that can be defined by the mod manager AND changed by the game
			root["RenderMethod"] = mAutoDetectRenderMethod ? "auto" :
								   (mRenderMethod == RenderMethod::OPENGL_FULL) ? "opengl-full" :
								   (mRenderMethod == RenderMethod::OPENGL_SOFT) ? "opengl-soft" : "software";
			root["Fullscreen"] = (int)mWindowMode;

			// Call subclass implementation
			saveSettingsInternal(root, SettingsType::GLOBAL);

			// Save file
			JsonHelper::saveFile(mSettingsFilenames[settingsIndex], root);
		}
	}

	// Save input settings
	settingsIndex = (int)SettingsType::INPUT;
	if (!mSettingsFilenames[settingsIndex].empty())
	{
		saveSettingsInput(mSettingsFilenames[settingsIndex]);
	}
}

void Configuration::loadConfigurationProperties(JsonHelper& rootHelper)
{
	// Read dev mode setting first, as other settings rely on it
	if (!mDevMode.mEnabled)	// If either config or settings set this to true, then it stays true
	{
		rootHelper.tryReadBool("DebugMode", mDevMode.mEnabled);		// Not a mistake -- this is intentional

		Json::Value devModeJson = rootHelper.mJson["DevMode"];
		if (devModeJson.isObject())
		{
			JsonHelper devModeHelper(devModeJson);
			devModeHelper.tryReadBool("Enabled", mDevMode.mEnabled);

			devModeHelper.tryReadString("LoadSaveState", mLoadSaveState);
			devModeHelper.tryReadInt("LoadLevel", mLoadLevel);
			devModeHelper.tryReadInt("UseCharacters", mUseCharacters);
			mUseCharacters = clamp(mUseCharacters, 0, 4);

			devModeHelper.tryReadBool("EnableROMDataAnalyser", mEnableROMDataAnalyser);
		}
	}

	// Paths
	if (mRomPath.empty())
	{
		if (rootHelper.tryReadString("RomPath", mRomPath))
		{
			FTX::FileSystem->normalizePath(mRomPath, false);
		}
	}
	if (rootHelper.tryReadString("ScriptsDir", mScriptsDir))
	{
		FTX::FileSystem->normalizePath(mScriptsDir, true);
	}
	rootHelper.tryReadString("MainScriptName", mMainScriptName);

	if (mDevMode.mEnabled)
	{
		if (rootHelper.tryReadString("SaveStatesDir", mSaveStatesDir))
		{
			FTX::FileSystem->normalizePath(mSaveStatesDir, true);
		}
	}

	// Platform
	rootHelper.tryReadInt("PlatformFlags", mPlatformFlags);

	// Game
	rootHelper.tryReadInt("StartPhase", mStartPhase);
	rootHelper.tryReadInt("GameRecording", mGameRecording);
	rootHelper.tryReadInt("GameRecPlayFrom", mGameRecPlayFrom);
	rootHelper.tryReadBool("GameRecIgnoreKeys", mGameRecIgnoreKeys);

	if (mLoadLevel != -1 || mGameRecording == 2)
	{
		// Enforce start phase 3 (in-game) when a level to load directly is defined, and in game recording playback mode
		mStartPhase = 3;
	}

	// Video
	tryParseWindowSize(rootHelper.mJson["WindowSize"].asString(), mWindowSize);
	if (mDevMode.mEnabled)
	{
		tryParseWindowSize(rootHelper.mJson["GameScreen"].asString(), mGameScreen);
	}
	rootHelper.tryReadInt("Upscaling", mUpscaling);
	rootHelper.tryReadInt("Filtering", mFiltering);
	rootHelper.tryReadInt("Scanlines", mScanlines);
	rootHelper.tryReadInt("BackgroundBlur", mBackgroundBlur);
	rootHelper.tryReadInt("PerformanceDisplay", mPerformanceDisplay);
	tryReadRenderMethod(rootHelper, mFailSafeMode, mRenderMethod, mAutoDetectRenderMethod);

	// Audio
	rootHelper.tryReadInt("AudioSampleRate", mAudioSampleRate);

	// Input recorder
	if (mDevMode.mEnabled)
	{
		JsonHelper jsonHelper(rootHelper.mJson["InputRecorder"]);
		jsonHelper.tryReadString("Playback", mInputRecorderInput);
		jsonHelper.tryReadString("Record", mInputRecorderOutput);
	}

	// Script
	rootHelper.tryReadInt("ScriptOptimizationLevel", mScriptOptimizationLevel);
#if DEBUG
	rootHelper.tryReadBool("CompileScripts", mForceCompileScripts);
#endif
}

void Configuration::saveSettingsInput(const std::wstring& filename) const
{
	// Using a custom JSON writer here to make the output easier to read, as standard formatting is kind of awful in this case

	std::vector<const InputConfig::DeviceDefinition*> sortedDeviceDefinitions;
	{
		// Sort input devices alphabetically, and so that keyboards are written first in any case
		sortedDeviceDefinitions.reserve(mInputDeviceDefinitions.size());
		for (size_t k = 0; k < mInputDeviceDefinitions.size(); ++k)
		{
			sortedDeviceDefinitions.push_back(&mInputDeviceDefinitions[k]);
		}
		std::sort(sortedDeviceDefinitions.begin(), sortedDeviceDefinitions.end(),
			[](const InputConfig::DeviceDefinition* a, const InputConfig::DeviceDefinition* b)
			{
				const bool keyboard_a = String(a->mIdentifier).startsWith("Keyboard");
				const bool keyboard_b = String(b->mIdentifier).startsWith("Keyboard");
				if (keyboard_a != keyboard_b)
				{
					return keyboard_a;
				}
				else
				{
					return a->mIdentifier < b->mIdentifier;
				}
			});
	}

	String output;
	lemon::SourceCodeWriter writer(output);

	writer.beginBlock();
	writer.writeLine("\"InputDevices\":");
	writer.beginBlock();

	for (size_t k = 0; k < sortedDeviceDefinitions.size(); ++k)
	{
		const InputConfig::DeviceDefinition& inputDeviceDefinition = *sortedDeviceDefinitions[k];

		writer.writeLine("\"" + inputDeviceDefinition.mIdentifier + "\":");
		writer.beginBlock();

		if (!inputDeviceDefinition.mDeviceNames.empty())
		{
			String line = "\"DeviceNames\": [ ";
			bool isFirst = true;
			for (const auto& pair : inputDeviceDefinition.mDeviceNames)
			{
				if (!isFirst)
					line << ", ";
				line << "\"" << pair.second << "\"";
				isFirst = false;
			}
			line << " ],";
			writer.writeLine(line);
		}

		for (size_t i = 0; i < (size_t)InputConfig::DeviceDefinition::Button::_NUM; ++i)
		{
			const std::string& name = InputMappingKeys[i];
			String line = "\"" + name + "\":";
			line.add(' ', 6 - (int)name.length());
			line << "[ ";

			const std::vector<InputConfig::Assignment>& assignments = inputDeviceDefinition.mMappings[i].mAssignments;
			bool isFirst = true;
			String str;
			for (const InputConfig::Assignment& assignment : assignments)
			{
				assignment.getMappingString(str, inputDeviceDefinition.mDeviceType);
				if (!isFirst)
					line << ", ";
				line << "\"" << str << "\"";
				isFirst = false;
			}
			line << " ]";
			if (i+1 < (size_t)InputConfig::DeviceDefinition::Button::_NUM)
				line << ",";
			writer.writeLine(line);
		}

		const bool lastBlock = (k+1 == mInputDeviceDefinitions.size());
		writer.endBlock(lastBlock ? "}" : "},");
	}

	writer.endBlock();
	writer.endBlock();
	output.saveFile(filename);
}
