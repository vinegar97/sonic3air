/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/Application.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/GameProfile.h"
#include "oxygen/application/audio/AudioOutBase.h"
#include "oxygen/application/input/ControlsIn.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/application/modding/ModManager.h"
#include "oxygen/application/video/VideoOut.h"
#include "oxygen/download/DownloadManager.h"
#include "oxygen/drawing/opengl/OpenGLDrawer.h"
#include "oxygen/drawing/software/SoftwareDrawer.h"
#include "oxygen/platform/CrashHandler.h"
#include "oxygen/platform/PlatformFunctions.h"
#include "oxygen/resources/FontCollection.h"
#include "oxygen/resources/ResourcesCache.h"
#include "oxygen/file/PackedFileProvider.h"
#include "oxygen/helper/FileHelper.h"
#include "oxygen/helper/Logging.h"
#include "oxygen/rendering/RenderResources.h"
#include "oxygen/simulation/LogDisplay.h"
#include "oxygen/simulation/PersistentData.h"
#include "oxygen/simulation/Simulation.h"
#if defined(PLATFORM_ANDROID)
	#include "oxygen/platform/AndroidJavaInterface.h"
#endif


#if !defined(PLATFORM_MAC) && !defined(PLATFORM_ANDROID)	// Maybe other platforms can be excluded as well? Possibly only Windows and Linux need this
	#define LOAD_APP_ICON_PNG
#endif


struct EngineMain::Internal
{
	GameProfile		mGameProfile;
	InputManager	mInputManager;
	LogDisplay		mLogDisplay;
	ModManager		mModManager;
	ResourcesCache	mResourcesCache;
	FontCollection	mFontCollection;
	PersistentData	mPersistentData;
	VideoOut		mVideoOut;
	ControlsIn		mControlsIn;
	DownloadManager mDownloadManager;

#if defined(PLATFORM_ANDROID)
	AndroidJavaInterface mAndroidJavaInterface;
#endif
};


void EngineMain::earlySetup()
{
	// This function contains stuff you would usually do right at the start of the "main" function

	// Setup crash handling
	CrashHandler::initializeCrashHandler();

#ifdef PLATFORM_WINDOWS
	// This fixes some audio issues with SDL 2.0.9 that some people faced
	// (possibly introduced earlier, only 2.0.4 is known to have worked)
	SDL_setenv("SDL_AUDIODRIVER", "directsound", true);
#endif

	SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");

	INIT_RMX;
	INIT_RMXEXT_OGGVORBIS;
}

EngineMain::EngineMain(EngineDelegateInterface& delegate_) :
	mDelegate(delegate_),
	mInternal(*new Internal())
{
}

EngineMain::~EngineMain()
{
	delete &mInternal;
}

void EngineMain::execute(int argc, char** argv)
{
	// Setup arguments
	mArguments.reserve(argc);
	for (int i = 0; i < argc; ++i)
	{
		mArguments.emplace_back(argv[i]);
	}

	// Startup the Oxygen engine part that is independent from the application / project
	if (startupEngine())
	{
		// Enter the application run loop
		run();
	}

	// Done, now shut everything down
	shutdown();
}

void EngineMain::onActiveModsChanged()
{
	// Update sprites
	RenderResources::instance().loadSpriteCache(true);

	// Update the resource cache -> palettes, raw data
	ResourcesCache::instance().loadAllResources();

	// Update fonts
	mInternal.mFontCollection.collectFromMods();

	// Update video
	mInternal.mVideoOut.handleActiveModsChanged();

	// Update audio
	mAudioOut->handleActiveModsChanged();

	// Scripts need to be reloaded
	Application::instance().getSimulation().reloadScriptsAfterModsChange();
}

bool EngineMain::reloadFilePackage(std::wstring_view packageName, bool forceReload)
{
	GameProfile& gameProfile = GameProfile::instance();
	for (size_t index = 0; index < gameProfile.mDataPackages.size(); ++index)
	{
		const GameProfile::DataPackage& dataPackage = gameProfile.mDataPackages[index];
		if (dataPackage.mFilename == packageName)
		{
			return loadFilePackageByIndex(index, forceReload);
		}
	}
	return false;
}

uint32 EngineMain::getPlatformFlags() const
{
	if (Configuration::instance().mPlatformFlags != -1)
	{
		return Configuration::instance().mPlatformFlags;
	}
	else
	{
		uint32 flags = 0;
	#if defined(PLATFORM_WINDOWS) || defined(PLATFORM_MAC) || defined(PLATFORM_LINUX)
		flags |= 0x0001;
	#elif defined(PLATFORM_ANDROID) || defined(PLATFORM_WEB) || defined(PLATFORM_IOS)
		flags |= 0x0002;
	#endif
		return flags;
	}
}

void EngineMain::switchToRenderMethod(Configuration::RenderMethod newRenderMethod)
{
	Configuration& config = Configuration::instance();
	const bool wasUsingOpenGL = (config.mRenderMethod == Configuration::RenderMethod::OPENGL_FULL || config.mRenderMethod == Configuration::RenderMethod::OPENGL_SOFT);
	config.mRenderMethod = newRenderMethod;

	bool nowUsingOpenGL = (config.mRenderMethod == Configuration::RenderMethod::OPENGL_FULL || config.mRenderMethod == Configuration::RenderMethod::OPENGL_SOFT);
	if (nowUsingOpenGL != wasUsingOpenGL)
	{
		// Need to recreate the window
		destroyWindow();
		createWindow();

		// Check OpenGL in the config again, it could have changed - namely if OpenGL initialization failed
		nowUsingOpenGL = (config.mRenderMethod == Configuration::RenderMethod::OPENGL_FULL || config.mRenderMethod == Configuration::RenderMethod::OPENGL_SOFT);
	}

	if (nowUsingOpenGL)
	{
		config.mAutoDetectRenderMethod = false;
	}

	// Switch the renderer
	VideoOut::instance().createRenderer(true);
}

void EngineMain::setVSyncMode(Configuration::FrameSyncType frameSyncMode)
{
	Configuration& config = Configuration::instance();
	if ((config.mRenderMethod == Configuration::RenderMethod::OPENGL_FULL) || (config.mRenderMethod == Configuration::RenderMethod::OPENGL_SOFT))
	{
		if (frameSyncMode >= Configuration::FrameSyncType::VSYNC_ON)
		{
			// First try adaptive V-Sync; if that's not supported, use regular V-Sync
			if (SDL_GL_SetSwapInterval(-1) < 0)
			{
				SDL_GL_SetSwapInterval(1);
			}
		}
		else
		{
			SDL_GL_SetSwapInterval(0);
		}
	}
}

bool EngineMain::startupEngine()
{
#if defined(PLATFORM_ANDROID)
	{
		// Create file provider for APK content access (and do it right here already)
		rmx::FileProviderSDL* provider = new rmx::FileProviderSDL();
		FTX::FileSystem->addManagedFileProvider(*provider);
		FTX::FileSystem->addMountPoint(*provider, L"", L"", 1);
	}
#endif

	PlatformFunctions::onEngineStartup();

	if (!mDelegate.onEnginePreStartup())
		return false;

	std::wstring argumentProjectPath;
#ifndef PLATFORM_ANDROID
	// Parse arguments
	for (size_t i = 1; i < mArguments.size(); ++i)
	{
		if (mArguments[i][0] == '-')
		{
			// TODO: Add handling for options
		}
		else
		{
			const String arg(mArguments[i]);

			std::wstring path = arg.toStdWString();
			FTX::FileSystem->normalizePath(path, true);
			if (FTX::FileSystem->exists(path + L"oxygenproject.json"))
			{
				argumentProjectPath = path;
			}
		}
	}
#endif

	const EngineDelegateInterface::AppMetaData& appMetaData = mDelegate.getAppMetaData();
	Configuration& config = Configuration::instance();

	// Don't use the accelerometer as a joystick on mobile devices, that's just confusing
	SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");

	// Disable the screen saver and hopefully also system sleep (which makes especially sense when playing with a game controller)
	//  -> It should be disabled by default according to the SDL2 docs, but that does not seem to be always the case
	SDL_DisableScreenSaver();

#ifndef PLATFORM_ANDROID
	config.mExePath = *String(mArguments[0]).toWString();
#ifndef PLATFORM_IOS
	// Choose app data path
	{
		const std::wstring appDataPath = PlatformFunctions::getAppDataPath();
		const bool useLocalSaveDataDirectory = (FTX::FileSystem->exists(L"savedata") || appMetaData.mAppDataFolder.empty() || appDataPath.empty());
		if (!useLocalSaveDataDirectory)
		{
			// This is the default case: Use the app data path
			config.mAppDataPath = appDataPath + L'/' + appMetaData.mAppDataFolder + L'/';
		}
		else
		{
			// Special case & fallback: Use local "savedata" path instead
			std::wstring currentDirectory = rmx::FileSystem::getCurrentDirectory();
			rmx::FileSystem::normalizePath(currentDirectory, true);
			config.mAppDataPath = currentDirectory + L"savedata/";
		}
	}
#endif
#else
	// Android
	// TODO: Use internal storage path as a fallback?
	WString storagePath = String(SDL_AndroidGetExternalStoragePath()).toWString();
	config.mAppDataPath = *(storagePath + L'/');
#endif

	config.mSaveStatesDirLocal = config.mAppDataPath + L"savestates/";
	config.mSRamFilename = config.mAppDataPath + L"sram.bin";
	config.mPersistentDataFilename = config.mAppDataPath + L"persistentdata.bin";

	// Startup logging
	{
		oxygen::Logging::startup(config.mAppDataPath + L"logfile.txt");
		RMX_LOG_INFO("--- STARTUP ---");
		RMX_LOG_INFO("Logging started");
		RMX_LOG_INFO("Application version: " << appMetaData.mBuildVersionString);

		String commandLine;
		for (std::string& arg : mArguments)
		{
			if (!commandLine.empty())
				commandLine.add(' ');
			commandLine.add(arg);
		}
		RMX_LOG_INFO("Command line:  " << commandLine.toStdString());
		RMX_LOG_INFO("App data path: " << WString(config.mAppDataPath).toStdString());
	}

	// Load configuration and settings
	if (!initConfigAndSettings(argumentProjectPath))
		return false;

	// Setup file system
	RMX_LOG_INFO("File system setup");
	if (!initFileSystem())
		return false;

	// System
	RMX_LOG_INFO("System initialization...");
	if (!FTX::System->initialize())
	{
		RMX_ERROR("System initialization failed", );
		return false;
	}

	// Video
	RMX_LOG_INFO("Video initialization...");
	if (!createWindow())
	{
		RMX_ERROR("Unable to create window" << (config.mFailSafeMode ? " in fail-safe mode" : "") << " with error: " << SDL_GetError(), );
		return false;
	}

	RMX_LOG_INFO("Startup of VideoOut");
	mInternal.mVideoOut.startup();

	// Input manager startup after config is loaded
	RMX_LOG_INFO("Input initialization...");
	InputManager::instance().startup();

	RMX_LOG_INFO("Startup of ControlsIn");
	mInternal.mControlsIn.startup();

	// Audio
	RMX_LOG_INFO("Audio initialization...");
	FTX::Audio->initialize(config.mAudioSampleRate, 2, 1024);

	RMX_LOG_INFO("Startup of AudioOut");
	mAudioOut = &EngineMain::getDelegate().createAudioOut();
	mAudioOut->startup();

	// Done
	RMX_LOG_INFO("Engine startup successful");
	return true;
}

void EngineMain::run()
{
	// Run RMX application
	RMX_LOG_INFO("");
	RMX_LOG_INFO("--- MAIN LOOP ---");
	RMX_LOG_INFO("Starting main application loop");

	Application application;
	FTX::System->run(application);
}

void EngineMain::shutdown()
{
	destroyWindow();

	// Shutdown subsystems
	mInternal.mVideoOut.shutdown();
	if (nullptr != mAudioOut)
	{
		mAudioOut->shutdown();
		SAFE_DELETE(mAudioOut);
	}
	mInternal.mControlsIn.shutdown();

	// Shutdown drawer
	mDrawer.shutdown();

	// Cleanup system
	RMX_LOG_INFO("System shutdown");
	FTX::Audio->exit();
	FTX::System->exit();
	FTX::JobManager->~JobManager();

	mInternal.mModManager.copyModSettingsToConfig();
	Configuration::instance().saveSettings();
	oxygen::Logging::shutdown();
}

bool EngineMain::initConfigAndSettings(const std::wstring& argumentProjectPath)
{
	RMX_LOG_INFO("Initializing configuration");
	Configuration& config = Configuration::instance();
	config.initialization();

	RMX_LOG_INFO("Loading configuration");
#if (defined(PLATFORM_MAC) || defined(PLATFORM_IOS)) && defined(ENDUSER)
	config.loadConfiguration(config.mGameDataPath + L"/config.json");
#else
	config.loadConfiguration(L"config.json");
#endif

	// Setup a custom game profile (like S3AIR does) or load the "oxygenproject.json"
	const bool hasCustomGameProfile = mDelegate.setupCustomGameProfile();
	if (!hasCustomGameProfile)
	{
		if (!argumentProjectPath.empty())
		{
			// Overwrite project path from config
			config.mProjectPath = argumentProjectPath;
		}
		if (!config.mProjectPath.empty())
		{
			RMX_LOG_INFO("Loading game profile");
			const bool loadedProject = mInternal.mGameProfile.loadOxygenProjectFromFile(config.mProjectPath + L"oxygenproject.json");
			RMX_CHECK(loadedProject, "Failed to load game profile from '" << *WString(config.mProjectPath).toString() << "oxygenproject.json'", );
		}
	}

	RMX_LOG_INFO("Loading settings");
	const bool loadedSettings = config.loadSettings(config.mAppDataPath + L"settings.json", Configuration::SettingsType::STANDARD);
	config.loadSettings(config.mAppDataPath + L"settings_input.json", Configuration::SettingsType::INPUT);
	config.loadSettings(config.mAppDataPath + L"settings_global.json", Configuration::SettingsType::GLOBAL);
	if (!loadedSettings)
	{
		// Save default settings once immediately
		config.saveSettings();
	}

	// Evaluate fail-safe mode
	if (config.mFailSafeMode)
	{
		RMX_LOG_INFO("Using fail-safe mode");
		config.mRenderMethod = Configuration::RenderMethod::SOFTWARE;	// Should already be set actually, but why not play it safe
	}
	else if (config.mRenderMethod == Configuration::RenderMethod::UNDEFINED)
	{
		config.mRenderMethod = Configuration::RenderMethod::OPENGL_FULL;
	}

#if defined(PLATFORM_ANDROID) || defined(PLATFORM_IOS)
	// Use fullscreen, with no borders please
	//  -> Note that this doesn't work for the web version, if running in mobile browsers - we rely on a window with fixed size (see config.json) there
	config.mWindowMode = Configuration::WindowMode::EXCLUSIVE_FULLSCREEN;
#endif

	config.evaluateGameRecording();

	RMX_LOG_INFO(((config.mRenderMethod == Configuration::RenderMethod::SOFTWARE) ? "Using pure software renderer" :
				  (config.mRenderMethod == Configuration::RenderMethod::OPENGL_SOFT) ? "Using opengl-soft renderer" : "Using opengl-full renderer"));
	return true;
}

bool EngineMain::initFileSystem()
{
	// Create mod data folder (the default mod directory)
	Configuration& config = Configuration::instance();
	FTX::FileSystem->createDirectory(config.mAppDataPath + L"mods");

	// Add real file system provider for the game data path, if it isn't located in local "data" directory
	//  -> This is relevant for Oxygen Engine using an external game data path
	if (config.mGameDataPath != L"data" && config.mGameDataPath != L"./data")
	{
		rmx::RealFileProvider* provider = new rmx::RealFileProvider();
		FTX::FileSystem->addManagedFileProvider(*provider);
		FTX::FileSystem->addMountPoint(*provider, L"data/", config.mGameDataPath + L'/', 0x10);
	}

	// Add package providers
	return loadFilePackages(false);
}

bool EngineMain::loadFilePackages(bool forceReload)
{
	Configuration& config = Configuration::instance();
	GameProfile& gameProfile = GameProfile::instance();
	mPackedFileProviders.resize(gameProfile.mDataPackages.size(), nullptr);

	for (size_t index = 0; index < gameProfile.mDataPackages.size(); ++index)
	{
		const bool success = loadFilePackageByIndex(index, forceReload);
		if (!success)
		{
			// Is this a required package after all?
			const GameProfile::DataPackage& dataPackage = gameProfile.mDataPackages[index];
			if (dataPackage.mRequired)
			{
				// We still accept missing packages if any data is present in unpacked form
				//  -> Just checking the "icon.png" to know whether that's the case
				static const bool hasUnpackedData = FTX::FileSystem->exists(config.mGameDataPath + L"/images/icon.png");
				RMX_CHECK(hasUnpackedData, "Could not find or open package '" << *WString(dataPackage.mFilename).toString() << "', application will close now again.", return false);
			}
		}
	}

	return true;
}

bool EngineMain::loadFilePackageByIndex(size_t index, bool forceReload)
{
	// Already loaded?
	if (nullptr != mPackedFileProviders[index])
	{
		if (forceReload)
		{
			FTX::FileSystem->destroyManagedFileProvider(*mPackedFileProviders[index]);
			mPackedFileProviders[index] = nullptr;
		}
		else
		{
			// Just ignore that one, it's already loaded
			return true;
		}
	}

	const GameProfile::DataPackage& dataPackage = GameProfile::instance().mDataPackages[index];
	Configuration& config = Configuration::instance();

	// First try loading from game installation
	const std::wstring gameDataBasePath = config.mGameDataPath + L"/";
	PackedFileProvider* provider = PackedFileProvider::createPackedFileProvider(gameDataBasePath + dataPackage.mFilename);
	if (nullptr == provider)
	{
		// Then try loading from save data (e.g. downloaded packages)
		const std::wstring saveDataBasePath = config.mAppDataPath + L"/data/";
		provider = PackedFileProvider::createPackedFileProvider(saveDataBasePath + dataPackage.mFilename);
	}

	if (nullptr != provider)
	{
		// Mount to "data" in any case, otherwise OxygenApp won't work when the game data path is somewhere different
		FTX::FileSystem->addManagedFileProvider(*provider);
		FTX::FileSystem->addMountPoint(*provider, L"data/", L"data/", 0x20 + (int)index);
		mPackedFileProviders[index] = provider;
		return true;
	}

	// Failed
	return false;
}

bool EngineMain::createWindow()
{
	Configuration& config = Configuration::instance();
	const EngineDelegateInterface::AppMetaData& appMetaData = mDelegate.getAppMetaData();
	
	const bool useOpenGL = (config.mRenderMethod == Configuration::RenderMethod::OPENGL_FULL) || (config.mRenderMethod == Configuration::RenderMethod::OPENGL_SOFT);

	// Setup video config
	rmx::VideoConfig videoConfig(config.mWindowMode != Configuration::WindowMode::WINDOWED, config.mWindowSize.x, config.mWindowSize.y, appMetaData.mTitle.c_str());
	videoConfig.mRenderer = useOpenGL ? rmx::VideoConfig::Renderer::OPENGL : rmx::VideoConfig::Renderer::SOFTWARE;
	videoConfig.mResizeable = true;
	videoConfig.mAutoClearScreen = useOpenGL;
	videoConfig.mAutoSwapBuffers = false;
	videoConfig.mVSync = (config.mFrameSync >= Configuration::FrameSyncType::VSYNC_ON);
	videoConfig.mIconResource = appMetaData.mWindowsIconResource;

	SDL_SetHint(SDL_HINT_RENDER_VSYNC, videoConfig.mVSync ? "1" : "0");

#if defined(LOAD_APP_ICON_PNG)
	// Load app icon
	if (!appMetaData.mIconFile.empty())
	{
		RMX_LOG_INFO("Loading application icon...");
		FileHelper::loadBitmap(videoConfig.mIconBitmap, appMetaData.mIconFile);
	}
#endif

	if (useOpenGL)
	{
		// Set SDL OpenGL attributes
		RMX_LOG_INFO("Setup of OpenGL attributes...");
	#if !defined(RMX_USE_GLES2)
		{
			// OpenGL 3.1 or 3.2
			const int majorVersion = 3;
		#if defined(PLATFORM_MAC)
			// macOS needs OpenGL 3.2 for GLSL 140 shaders to work. https://stackoverflow.com/a/31805596
			const int minorVersion = 2;
		#else
			const int minorVersion = 1;

			SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
			SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		#endif

			RMX_LOG_INFO("Using OpenGL " << majorVersion << "." << minorVersion);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, majorVersion);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minorVersion);
		}
	#else
		{
			// GL ES 2.0
			const int majorVersion = 2;
			const int minorVersion = 0;

			RMX_LOG_INFO("Using OpenGL ES " << majorVersion << "." << minorVersion);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, majorVersion);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minorVersion);
		}
	#endif
	}

	// Create window
	{
		const int displayIndex = config.mDisplayIndex;

		uint32 flags = useOpenGL ? SDL_WINDOW_OPENGL : 0;
		switch (config.mWindowMode)
		{
			case Configuration::WindowMode::WINDOWED:
			{
				// (Non-maximized) Window
				if (videoConfig.mResizeable)
					flags |= SDL_WINDOW_RESIZABLE;
				break;
			}

			case Configuration::WindowMode::BORDERLESS_FULLSCREEN:
			{
				// Borderless maximized window
				SDL_Rect rect;
				if (SDL_GetDisplayBounds(displayIndex, &rect) == 0)
				{
					videoConfig.mWindowRect.width = rect.w;
					videoConfig.mWindowRect.height = rect.h;
				}
				else
				{
					SDL_DisplayMode dm;
					if (SDL_GetDesktopDisplayMode(displayIndex, &dm) == 0)
					{
						videoConfig.mWindowRect.width = dm.w;
						videoConfig.mWindowRect.height = dm.h;
					}
				}
				flags |= SDL_WINDOW_BORDERLESS;
				break;
			}

			case Configuration::WindowMode::EXCLUSIVE_FULLSCREEN:
			{
				// Fullscreen window at desktop resolution
				//  -> According to https://wiki.libsdl.org/SDL_SetWindowFullscreen, this is not really an exclusive fullscreen mode, but that's fine
				flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
				break;
			}
		}

		RMX_LOG_INFO("Creating window...");
		mSDLWindow = SDL_CreateWindow(*videoConfig.mCaption, SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex), videoConfig.mWindowRect.width, videoConfig.mWindowRect.height, flags);
		if (nullptr == mSDLWindow)
		{
			return false;
		}

		RMX_LOG_INFO("Retrieving actual window size...");
		SDL_GetWindowSize(mSDLWindow, &videoConfig.mWindowRect.width, &videoConfig.mWindowRect.height);
		SDL_ShowCursor(!videoConfig.mHideCursor);

		if (useOpenGL)
		{
			RMX_LOG_INFO("Creating OpenGL context...");
			SDL_GLContext context = SDL_GL_CreateContext(mSDLWindow);
			if (nullptr != context)
			{
				RMX_LOG_INFO("Vsync setup...");
				setVSyncMode(config.mFrameSync);
			}
			else
			{
				RMX_LOG_INFO("Failed to create OpenGL context, fallback to pure software renderer");
				config.mRenderMethod = Configuration::RenderMethod::SOFTWARE;
				// TODO: In this case, the SDL window was created with SDL_WINDOW_OPENGL flag, but that does not seem to be a problem
			}
		}
	}

	// Create drawer depending on render method
#ifdef RMX_WITH_OPENGL_SUPPORT
	if (config.mRenderMethod >= Configuration::RenderMethod::OPENGL_SOFT)
	{
		if (!mDrawer.createDrawer<OpenGLDrawer>())
		{
			// Fallback to software drawer
			RMX_LOG_INFO("OpenGL drawer setup failed, using software rendering");
			config.mRenderMethod = Configuration::RenderMethod::SOFTWARE;
			mDrawer.createDrawer<SoftwareDrawer>();
		}
	}
	else
#endif
	{
		mDrawer.createDrawer<SoftwareDrawer>();
	}

	// Tell FTX video manager that everything is okay
	FTX::Video->setInitialized(videoConfig, mSDLWindow);

#if defined(PLATFORM_WINDOWS)
	// Set window icon (using a Windows-specific method)
	if (videoConfig.mIconResource != 0)
	{
		RMX_LOG_INFO("Setting window icon (Windows)...");
		PlatformFunctions::setAppIcon(videoConfig.mIconResource);
	}
#endif

#if defined(LOAD_APP_ICON_PNG)
	// Set window icon (using SDL functionality)
	if (nullptr != videoConfig.mIconBitmap.getData() || videoConfig.mIconSource.nonEmpty())
	{
		RMX_LOG_INFO("Setting window icon from loaded bitmap...");
		Bitmap tmp;
		Bitmap* bitmap = &videoConfig.mIconBitmap;
		if (bitmap->empty())
		{
			bitmap = nullptr;
			if (tmp.load(videoConfig.mIconSource.toWString()))
			{
				bitmap = &tmp;
			}
		}

		if (nullptr != bitmap)
		{
			bitmap->rescale(32, 32);
			SDL_Surface* icon = SDL_CreateRGBSurfaceFrom(bitmap->getData(), 32, 32, 32, bitmap->getWidth() * sizeof(uint32), 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
			SDL_SetWindowIcon(mSDLWindow, icon);
			SDL_FreeSurface(icon);
		}
	}
#endif

	return true;
}

void EngineMain::destroyWindow()
{
	mInternal.mVideoOut.destroyRenderer();
	mDrawer.destroyDrawer();
	SDL_DestroyWindow(mSDLWindow);
	mSDLWindow = nullptr;
}
