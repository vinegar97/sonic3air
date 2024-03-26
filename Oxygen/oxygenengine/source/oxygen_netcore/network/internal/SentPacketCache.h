/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "oxygen_netcore/network/internal/SentPacket.h"


class SentPacketCache
{
public:
	void clear();
	uint32 getNextUniquePacketID() const;

	void addPacket(SentPacket& sentPacket, uint64 currentTimestamp, bool isStartConnectionPacket = false);

	void onPacketReceiveConfirmed(uint32 uniquePacketID);

	inline bool hasUnconfirmedPackets() const  { return !mQueue.empty(); }
	void updateResend(std::vector<SentPacket*>& outPacketsToResend, uint64 currentTimestamp);

private:
	uint32 mQueueStartUniquePacketID = 1;
	uint32 mNextUniquePacketID = 1;		// This should always be "mQueueStartUniquePacketID + mQueue.size()"
	std::deque<SentPacket*> mQueue;		// Can contain null pointers, anmely at the positions of packets that were already confirmed by the receiver
};
