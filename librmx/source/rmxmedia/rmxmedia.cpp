/*
*	rmx Library
*	Copyright (C) 2008-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "rmxmedia.h"
#include "buildinfo.inc"


// Singletons
namespace FTX
{
	SingletonPtr<rmx::JobManager>		 JobManager;
	SingletonPtr<rmx::FTX_SystemManager> System;
	SingletonPtr<rmx::FTX_VideoManager>	 Video;
	SingletonPtr<rmx::AudioManager>		 Audio;
	SingletonPtr<rmx::Painter>			 Painter;
};


void rmxmedia::initialize()
{
	rmxbase::initialize();

	// Initialize audio load callbacks
	AudioBuffer::LoadCallbackList callbacks;
	callbacks.push_back(rmx::WavLoader::load);

	AudioBuffer::LoadCallbackList& cblist = AudioBuffer::mStaticLoadCallbacks;
	for (const AudioBuffer::LoadCallbackType callback : callbacks)
	{
		if (!containsElement(cblist, callback))
		{
			cblist.push_back(callback);
		}
	}

	// Initialize font factories
	Font::mCodecs.add<FontSourceStdFactory>();
	Font::mCodecs.add<FontSourceBitmapFactory>();
}

void rmxmedia::getBuildInfo(String& info)
{
	info.clear() << "rmxmedia";
#ifdef DEBUG
	info << "D";
#endif
	String bnum;
	bnum << BUILD_NUMBER;
	bnum.fillLeft('0', 4);
	info << " " << RMXMEDIA_VERSION << "." << bnum << "  ";
#ifdef _MSC_VER
	info << "MSC " << (_MSC_VER / 100) << "." << (_MSC_VER % 100);
#elif defined(__GNUC__)
	info << "GCC " << __GNUC__ << "." << __GNUC_MINOR__ << "." << __GNUC_PATCHLEVEL__;
#else
	info << "unknown compiler";
#endif
}
