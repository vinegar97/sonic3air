/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2022 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/EngineDelegate.h"
#include "sonic3air/helper/ArgumentsReader.h"
#include "sonic3air/helper/PackageBuilder.h"

#include "oxygen/base/PlatformFunctions.h"
#include "oxygen/file/FilePackage.h"
#ifdef PLATFORM_SWITCH
#include <switch.h>

#ifdef DEBUG
extern "C" {

extern char __start__;
extern char __rodata_start;

void HandleFault(uint64_t pc, uint64_t lr, uint64_t fp, uint64_t faultAddr,
                 uint32_t desc) {
  if (pc >= (uint64_t)&__start__ && pc < (uint64_t)&__rodata_start) {
    printf(
        "unintentional fault in .text at %p (type %d) (trying to access %p?)\n",
        (void *)(pc - (uint64_t)&__start__), desc, (void *)faultAddr);

    int frameNum = 0;
    while (true) {
      printf("stack frame %d %p\n", frameNum,
             (void *)(lr - (uint64_t)&__start__));
      lr = *(uint64_t *)(fp + 8);
      fp = *(uint64_t *)fp;

      frameNum++;
      if (frameNum > 16 || fp == 0 || (fp & 0x7) != 0)
        break;
    }
  } else {
    printf("unintentional fault somewhere in deep (address) space at %p (type "
           "%d)\n",
           (void *)pc, desc);
    if (lr >= (uint64_t)&__start__ && lr < (uint64_t)&__rodata_start)
      printf("lr in range: %p\n", (void *)(lr - (uint64_t)&__start__));
  }
}

void QuickContextRestore(u64 *) __attribute__((noreturn));

alignas(16) uint8_t __nx_exception_stack[0x8000];
uint64_t __nx_exception_stack_size = 0x8000;

void __libnx_exception_handler(ThreadExceptionDump *ctx) {
  HandleFault(ctx->pc.x, ctx->lr.x, ctx->fp.x, ctx->far.x, ctx->error_desc);
}
}

#include <unistd.h>
#define TRACE(fmt, ...)                                                        \
  printf("%s: " fmt "\n", __PRETTY_FUNCTION__, ##__VA_ARGS__)

static int s_nxlinkSock = -1;

static void initNxLink() {
  if (R_FAILED(socketInitializeDefault()))
    return;

  s_nxlinkSock = nxlinkStdio();
  if (s_nxlinkSock >= 0)
    TRACE("printf output now goes to nxlink server");
  else
    socketExit();
}

static void deinitNxLink() {
  if (s_nxlinkSock >= 0) {
    close(s_nxlinkSock);
    socketExit();
    s_nxlinkSock = -1;
  }
}

extern "C" void userAppInit() { initNxLink(); }

extern "C" void userAppExit() { deinitNxLink(); }
#endif
#endif

// HJW: I know it's sloppy to put this here... it'll get moved afterwards
// Building with my env (msys2,gcc) requires this stub for some reason
#ifndef pathconf
long pathconf(const char* path, int name)
{
	errno = ENOSYS;
	return -1;
}
#endif

#if defined(PLATFORM_WINDOWS) & !defined(__GNUC__)
extern "C"
{
	_declspec(dllexport) uint32 NvOptimusEnablement = 1;
	_declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif


int main(int argc, char** argv)
{
	EngineMain::earlySetup();

	// Read command line arguments
	ArgumentsReader arguments;
	arguments.read(argc, argv);

	// For certain arguments, just try to forward them to an already running instance of S3AIR
	if (!arguments.mUrl.empty())
	{
		if (CommandForwarder::trySendCommand("ForwardedCommand:Url:" + arguments.mUrl))
			return 0;
	}

	// Make sure we're in the correct working directory
	PlatformFunctions::changeWorkingDirectory(arguments.mExecutableCallPath);

#if !defined(ENDUSER) && !defined(PLATFORM_ANDROID)
	if (arguments.mPack)
	{
		PackageBuilder::performPacking();
		if (!arguments.mNativize && !arguments.mDumpCppDefinitions)		// In case multiple arguments got combined, the others would get ignored without this check
			return 0;
	}
#endif

	try
	{
		// Create engine delegate and engine main instance
		EngineDelegate myDelegate;
		EngineMain myMain(myDelegate);

		// Evaluate some more arguments
		Configuration& config = Configuration::instance();
		if (arguments.mNativize)
		{
			config.mRunScriptNativization = 1;
			config.mScriptNativizationOutput = L"source/sonic3air/_nativized/NativizedCode.inc";
			config.mExitAfterScriptLoading = true;
		}
		if (arguments.mDumpCppDefinitions)
		{
			config.mDumpCppDefinitionsOutput = L"scripts/_reference/cpp_core_functions.lemon";
			config.mExitAfterScriptLoading = true;
		}

		// Now run the game
		myMain.execute(argc, argv);
	}
	catch (const std::exception& e)
	{
		RMX_ERROR("Caught unhandled exception in main loop: " << e.what(), );
	}

	return 0;
}
