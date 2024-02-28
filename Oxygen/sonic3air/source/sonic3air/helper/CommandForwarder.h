/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include <rmxbase.h>


class CommandForwarder
{
public:
	static bool trySendCommand(std::string_view command);

public:
	CommandForwarder();
	~CommandForwarder();

	void startup();
	void shutdown();
	void update(float deltaSeconds);

	void handleReceivedCommand(std::string_view command);

private:
	struct Internal;
	Internal& mInternal;
};
