/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/menu/GameApp.h"
#include "sonic3air/menu/GameMenuManager.h"
#include "sonic3air/menu/MenuBackground.h"
#include "sonic3air/menu/PauseMenu.h"
#include "sonic3air/menu/SharedResources.h"
#include "sonic3air/menu/TimeAttackResultsMenu.h"
#include "sonic3air/menu/context/ApplicationContextMenu.h"
#include "sonic3air/menu/overlays/SecretUnlockedWindow.h"
#include "sonic3air/menu/overlays/SkippableCutsceneWindow.h"
#include "sonic3air/audio/AudioOut.h"
#include "sonic3air/data/SharedDatabase.h"
#include "sonic3air/Game.h"

#include "oxygen/application/Application.h"
#include "oxygen/application/Configuration.h"
#include "oxygen/application/EngineMain.h"
#include "oxygen/application/input/InputManager.h"
#include "oxygen/application/mainview/GameView.h"
#include "oxygen/helper/FileHelper.h"
#include "oxygen/platform/PlatformFunctions.h"
#include "oxygen/rendering/utils/RenderUtils.h"
#include "oxygen/simulation/EmulatorInterface.h"
#include "oxygen/simulation/Simulation.h"


namespace
{
	static const constexpr float DISCLAIMER_FADE_IN_TIME  = 0.8f;
	static const constexpr float DISCLAIMER_FADE_OUT_TIME = 0.2f;
}


GameApp::GameApp() :
	mGameMenuManager(new GameMenuManager()),
	mMenuBackground(new MenuBackground()),
	mPauseMenu(new PauseMenu()),
	mTimeAttackResultsMenu(new TimeAttackResultsMenu())
{
	mGameMenuManager->initWithRoot(*mMenuBackground);
}

GameApp::~GameApp()
{
	delete mGameMenuManager;
	delete mMenuBackground;
	delete mPauseMenu;
	delete mTimeAttackResultsMenu;
	delete mSecretUnlockedWindow;
	delete mSkippableCutsceneWindow;
}

void GameApp::initialize()
{
	// Init shared resources
	global::loadSharedResources();

	mGameView = &Application::instance().getGameView();
	Simulation& simulation = Application::instance().getSimulation();
	simulation.setRunning(false);

	gotoPhase(Configuration::instance().mStartPhase);
	if (Configuration::instance().mLoadLevel >= 0)
	{
		Game::instance().startIntoLevel(Game::Mode::UNDEFINED, 0, Configuration::instance().mLoadLevel, Configuration::instance().mUseCharacters);
	}

	if (nullptr == mApplicationContextMenu)
	{
		mApplicationContextMenu = createChild<ApplicationContextMenu>();
	}
}

void GameApp::deinitialize()
{
	// Remove children that get explicitly deleted
	mGameView->removeChild(mMenuBackground);
	mGameView->removeChild(mPauseMenu);
	mGameView->removeChild(mTimeAttackResultsMenu);
	if (nullptr != mSecretUnlockedWindow)
		mGameView->removeChild(mSecretUnlockedWindow);
	if (nullptr != mSkippableCutsceneWindow)
		mGameView->removeChild(mSkippableCutsceneWindow);
}

void GameApp::mouse(const rmx::MouseEvent& ev)
{
	GuiBase::mouse(ev);
}

void GameApp::keyboard(const rmx::KeyboardEvent& ev)
{
	GuiBase::keyboard(ev);
}

void GameApp::update(float timeElapsed)
{
	GuiBase::update(timeElapsed);

	// Update disclaimer fade-in / fade-out
	{
		const float dt = std::min(timeElapsed, 0.05f);
		if (mCurrentState == State::DISCLAIMER)
		{
			mDisclaimerVisibility = std::min(mDisclaimerVisibility + dt / DISCLAIMER_FADE_IN_TIME, 1.0f);

			mStateTimeout -= dt;
			if (mStateTimeout <= 0.0f || InputManager::instance().anythingPressed())
			{
				gotoPhase(1);
			}
		}
		else
		{
			if (mDisclaimerVisibility > 0.0f)
			{
				mDisclaimerVisibility -= dt / DISCLAIMER_FADE_OUT_TIME;
				if (mDisclaimerVisibility <= 0.0f)
				{
					mDisclaimerVisibility = 0.0f;
					mDisclaimerTexture.clearBitmap();	// Unload to save on RAM
				}
			}
		}
	}

	if (mCurrentState == State::INGAME)
	{
		// Input
		Game::instance().updateSpecialInput(timeElapsed);
	}

	// GUI
	mGameMenuManager->updateMenus();

	if (nullptr != mPauseMenu->getParent() && mPauseMenu->canBeRemoved())
	{
		mGameView->removeChild(mPauseMenu);
	}
	if (nullptr != mRemoveChild && mRemoveChild->getParent() == mGameView)
	{
		mGameView->removeChild(mRemoveChild);
		mRemoveChild = nullptr;
	}

	// Make sure the overlay windows are always on top
	if (nullptr != mSecretUnlockedWindow && nullptr != mSecretUnlockedWindow->getParent())
	{
		mSecretUnlockedWindow->getParent()->moveToFront(mSecretUnlockedWindow);
	}
	if (nullptr != mSkippableCutsceneWindow && nullptr != mSkippableCutsceneWindow->getParent())
	{
		if (mSkippableCutsceneWindow->canBeRemoved())
		{
			mGameView->removeChild(mSkippableCutsceneWindow);
		}
		else
		{
			mSkippableCutsceneWindow->getParent()->moveToFront(mSkippableCutsceneWindow);
		}
	}
}

void GameApp::render()
{
	Drawer& drawer = EngineMain::instance().getDrawer();

	if (mDisclaimerVisibility > 0.0f)
	{
		const Rectf rect = RenderUtils::getLetterBoxRect(FTX::screenRect(), (float)mDisclaimerTexture.getWidth() / (float)mDisclaimerTexture.getHeight());
		drawer.setBlendMode(BlendMode::OPAQUE);
		drawer.setSamplingMode(SamplingMode::BILINEAR);
		drawer.drawRect(rect, mDisclaimerTexture, Color(mDisclaimerVisibility, mDisclaimerVisibility, mDisclaimerVisibility));
		drawer.setSamplingMode(SamplingMode::POINT);
		drawer.performRendering();
	}

	GuiBase::render();
}

void GameApp::onStartGame()
{
	mCurrentState = State::INGAME;
	mRemoveChild = mMenuBackground;
	mGameView->setFadedIn();
}

void GameApp::openTitleScreen()
{
	mRemoveChild = mMenuBackground;
	mGameView->setFadedIn();
	gotoPhase(1);
}

void GameApp::openMainMenu()
{
	Application::instance().getSimulation().setRunning(false);
	AudioOut::instance().stopSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_MUSIC);
	AudioOut::instance().stopSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);

	if (mPauseMenu->getParent() == mGameView)
		mGameView->removeChild(mPauseMenu);
	if (mTimeAttackResultsMenu->getParent() == mGameView)
		mGameView->removeChild(mTimeAttackResultsMenu);

	mCurrentState = State::MAIN_MENU;
	mGameView->addChild(mMenuBackground);
	mGameView->startFadingIn();

	mGameMenuManager->forceRemoveAll();
	mMenuBackground->openGameStartedMenu();

	Game::instance().resetCurrentMode();
}

void GameApp::openOptionsMenuInGame()
{
	mCurrentState = State::INGAME_OPTIONS;

	mPauseMenu->setEnabled(false);
	mGameView->addChild(mMenuBackground);
	mGameView->startFadingIn();
	mMenuBackground->openOptions(true);
}

void GameApp::onExitOptions()
{
	if (mCurrentState == State::INGAME_OPTIONS)
	{
		// Only start fading to black - see "onFadedOutOptions" for the actual change of state after complete fade-out
		GameApp::instance().getGameView().startFadingOut(0.1666f);
	}
	else
	{
		mMenuBackground->openMainMenu();
	}
}

void GameApp::onFadedOutOptions()
{
	if (mCurrentState == State::INGAME_OPTIONS)
	{
		// Coming from in-game options, then go back into the game
		if (mMenuBackground->getParent() == mGameView)
			mGameView->removeChild(mMenuBackground);

		mPauseMenu->setEnabled(true);
		mPauseMenu->onReturnFromOptions();

		GameApp::instance().getGameView().startFadingIn(0.1f);

		// TODO: Fade out the context instead
		AudioOut::instance().stopSoundContext(AudioOut::CONTEXT_MENU + AudioOut::CONTEXT_MUSIC);

		mCurrentState = State::INGAME;
	}
}

void GameApp::onExitExtras()
{
	mMenuBackground->openMainMenu();
}

void GameApp::onExitMods()
{
	mMenuBackground->openMainMenu();
}

void GameApp::onGamePaused(bool canRestart)
{
	Application::instance().getSimulation().setSpeed(0.0f);
	AudioOut::instance().pauseSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_MUSIC);
	AudioOut::instance().pauseSoundContext(AudioOut::CONTEXT_INGAME + AudioOut::CONTEXT_SOUND);

	mPauseMenu->enableRestart(canRestart);
	mPauseMenu->onFadeIn();
	if (nullptr == mPauseMenu->getParent())
	{
		mGameView->addChild(mPauseMenu);
	}
}

void GameApp::onGameResumed()
{
	// Not used at the moment
}

void GameApp::restartTimeAttack()
{
	mCurrentState = State::INGAME;
	Game::instance().restartTimeAttack(true);
	mGameView->removeChild(mTimeAttackResultsMenu);
}

void GameApp::returnToMenu()
{
	openMainMenu();
}

void GameApp::showTimeAttackResults(int hundreds, const std::vector<int>& otherTimes)
{
	mCurrentState = State::TIME_ATTACK_RESULTS;
	if (mTimeAttackResultsMenu->getParent() != mGameView)
	{
		mTimeAttackResultsMenu->setYourTime(hundreds);
		for (int time : otherTimes)
		{
			mTimeAttackResultsMenu->addOtherTime(time);
		}
		mTimeAttackResultsMenu->onFadeIn();

		mGameView->addChild(mTimeAttackResultsMenu);
	}
}

void GameApp::enableStillImageBlur(bool enable, float timeout)
{
	mGameView->setStillImageMode(enable ? GameView::StillImageMode::BLURRING : GameView::StillImageMode::NONE, timeout);
}

void GameApp::showUnlockedWindow(SecretUnlockedWindow::EntryType entryType, const std::string& title, const std::string& content)
{
	if (nullptr == mSecretUnlockedWindow)
	{
		mSecretUnlockedWindow = new SecretUnlockedWindow();
	}
	mGameView->addChild(mSecretUnlockedWindow);
	mSecretUnlockedWindow->show(entryType, title, content, (entryType == SecretUnlockedWindow::EntryType::SECRET) ? 0x68 : 0x63);
}

void GameApp::showSkippableCutsceneWindow(bool show)
{
	if (nullptr == mSkippableCutsceneWindow)
	{
		if (!show)
			return;
		mSkippableCutsceneWindow = new SkippableCutsceneWindow();
		mGameView->addChild(mSkippableCutsceneWindow);
	}
	else
	{
		if (nullptr == mSkippableCutsceneWindow->getParent())
			mGameView->addChild(mSkippableCutsceneWindow);
	}
	mSkippableCutsceneWindow->show(show);
}

void GameApp::gotoPhase(int phaseNumber)
{
	switch (phaseNumber)
	{
		case 0:
		{
			// Start with the disclaimer
			mCurrentState = State::DISCLAIMER;
			mStateTimeout = 8.0f;
			InputManager::instance().setTouchInputMode(InputManager::TouchInputMode::FULLSCREEN_START);

			// Load disclaimer texture if not done already
			if (!mDisclaimerTexture.isValid())
			{
				FileHelper::loadTexture(mDisclaimerTexture, L"data/images/menu/disclaimer.png");
			}
			break;
		}

		case 1:
		{
			// Start with the intro & title screen
			mCurrentState = State::TITLE_SCREEN;
			Game::instance().startIntoTitleScreen();
			break;
		}

		case 2:
		{
			// Start with the main menu
			openMainMenu();
			break;
		}

		case 3:
		{
			// Start in-game
			mCurrentState = State::INGAME;
			Game::instance().resetCurrentMode();
			Simulation& simulation = Application::instance().getSimulation();
			simulation.setRunning(true);
			break;
		}
	}
}
