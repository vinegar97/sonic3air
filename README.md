# ![Sonic 3 A.I.R.](Oxygen/soncthrickles/data/images/menu/sonic3air_logo.png)

Source code incl. dependencies for "Sonic 3 - Angel Island Revisited", a fan-made remaster of Sonic 3 & Knuckles.

Project homepage: https://sonic3air.org/


### Disclaimer

Sonic 3 A.I.R. is a non-profit fan game project. It is not affiliated in any way with SEGA or Sonic Team, the original creators of Sonic 3 and Sonic & Knuckles.

Sonic the Hedgehog is a trademark of SEGA. All copyrights regarding Sonic the Hedgehog, including characters, names, terms, art, and music belong to SEGA. All registered trademarks belong to SEGA and Sonic Team.

The developers of Sonic 3 A.I.R. have no intent to infringe said copyrights and registered trademarks.
No financial gain is made from this project.

Any commercial use of this project without SEGA's explicit consent is strictly prohibited.


## Repository overview

This repository is split into several different projects:
* The larger external dependencies (namely SDL2, Ogg/Vorbis, zlib) inside the ["framework"](framework/) directory. These are copies of the respective open source projects, with a few custom changes applied where needed - see the "how-to-build.txt" files in there for details.
* The librmx libraries that together with the external dependencies build a foundation for my own projects (S3AIR and my private stuff). This primarily consists of rmxbase, a collection of helper and utility classes, and rmxmedia, which is providing a basic game framework built on top of SDL2 & OpenGL.
* Lemonscript language library, with compiler and runtime environment for script execution.
* Oxygen Engine, the backbone game engine powering Sonic 3 A.I.R. This includes input, graphics, audio handling, and all the other game application stuff, as well as a simulation environment built around lemonscript that - as far as needed for the purposes of S3AIR - emulates aspects of Sega Genesis hardware. Note that Oxygen can be built as its own application (OxygenApp) that lacks the S3AIR C++ code.
* S3AIR-specific C++ code, scripts and data in the ["Oxygen/soncthrickles"](Oxygen/soncthrickles) directory. Yes, that's what it's named.


## External dependencies

External libraries and code used in this project:
* SDL2 - in ["framework/external/sdl"](framework/external/sdl)
* libogg & libvorbis - in ["framework/external/ogg-vorbis"](framework/external/ogg-vorbis)
* zlib incl. minizip - in ["framework/external/zlib"](framework/external/zlib)
* jsoncpp - in ["librmx/source/rmxbase/jsoncpp"](librmx/source/rmxbase/jsoncpp)
* GLEW - in "[librmx/source/rmxmedia/glew"](librmx/source/rmxmedia/glew)
* Sound chip emulation related code from Genesis Plus GX - in ["Oxygen/oxygenengine/source/oxygen/simulation/sound"](Oxygen/oxygenengine/source/oxygen/simulation/sound)
* Discord Game SDK - in ["Oxygen/soncthrickles/source/external/discord_game_sdk"](Oxygen/soncthrickles/source/external/discord_game_sdk)
* xBRZ upscaler shader code - in ["Oxygen/oxygenengine/data/shader"](Oxygen/oxygenengine/data/shader) and once more in ["Oxygen/soncthrickles/data/shader"](Oxygen/soncthrickles/data/shader)
* Hqx upscaler shader code & data files - in ["Oxygen/oxygenengine/data/shader"](Oxygen/oxygenengine/data/shader) and once more in ["Oxygen/soncthrickles/data/shader"](Oxygen/soncthrickles/data/shader)


## How to build

For information on how to build for different platforms, find the readme files in the respective subdirectories of "Oxygen/soncthrickles":
* Windows: ["build/_vstudio"](Oxygen/soncthrickles/build/_vstudio)
* Mac:     ["build/_xcode"](Oxygen/soncthrickles/build/_xcode)
* Linux:   ["build/_cmake"](Oxygen/soncthrickles/build/_cmake)
* Android: ["build/_android"](Oxygen/soncthrickles/build/_android)
* Web:     ["build/_emscripten"](Oxygen/soncthrickles/build/_emscripten)
* Switch:  ["build/_make"](Oxygen/soncthrickles/build/_make)


## Want to contribute?

Here's the bad news: This repository isn't meant for direct distribution. If you have your own changes that you want to share with the world, create a **fork** of this repo, instead of making pull requests. I'd very much appreciate that as I really want to avoid having too much overhead with managing / reviewing code changes made by others. I'd rather use the time to continue with implementing my own ideas into the project.

Plus there's a second reason, and that's an important one for me as well: It's about code ownership and software licenses. It's much easier to use the librmx, lemonscript and Oxygen Engine code elsewhere under a different license than GPL if I don't have to ask a larger group of contributors whether they are okay with it. Because there are some plans for possible future projects using these codes as a foundation, maybe even commercial ones where GPL could make things complicated.

-- Euka
