/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "sonic3air/pch.h"
#include "sonic3air/client/crowdcontrol/CrowdControlClient.h"

#include "oxygen/application/Application.h"
#include "oxygen/simulation/CodeExec.h"
#include "oxygen/simulation/Simulation.h"


bool CrowdControlClient::startConnection()
{
	if (mSetupDone)
	{
		// Already connected?
		if (mSocket.isValid())
			return true;
		mSetupDone = false;
	}

	Sockets::startupSockets();

	// Assume a locally running instance of the Crowd Control app
	if (!mSocket.connectTo("127.0.0.1", 58430))
		return false;

	// Done
	mSetupDone = true;
	return true;
}

void CrowdControlClient::stopConnection()
{
	mSocket.close();
	mSetupDone = false;
}

void CrowdControlClient::updateConnection(float timeElapsed)
{
#if 0
	// Test for script function call if not connected to CC
	{
		static float timeout = 3.0f;
		timeout -= std::min(timeElapsed, 0.05f);
		if (timeout < 0.0f)
		{
			triggerEffect("AddRing");
			timeout = 5.0f;
		}
	}
#endif

	if (!mSetupDone)
		return;

	TCPSocket::ReceiveResult result;
	if (mSocket.receiveNonBlocking(result))
	{
		std::string errors;
		Json::Value jsonRoot = rmx::JsonHelper::loadFromMemory(result.mBuffer, &errors);
		if (jsonRoot.isObject())
		{
			evaluateMessage(jsonRoot);
		}
	}
}

void CrowdControlClient::evaluateMessage(const Json::Value& message)
{
	// Read properties from the JSON message
	const std::string code = message["code"].asString();
	const std::string viewer = message["viewer"].asString();
	// TODO: https://github.com/BttrDrgn/ccpp/blob/master/ccpp.cpp removes double quotes " here for code and viewer, is this needed for us as well?
	const int id = message["id"].asInt();

	// Trigger the effect
	const StatusCode statusCode = triggerEffect(code);

	// Send back a response
	const std::string response = "{\"id\":" + std::to_string(id) + ",\"status\":" + std::to_string((int)statusCode) + "}";
	mSocket.sendData((const uint8*)response.c_str(), response.length() + 1);
}

CrowdControlClient::StatusCode CrowdControlClient::triggerEffect(const std::string& effectCode)
{
	// Prepare and execute script call
	CodeExec& codeExec = Application::instance().getSimulation().getCodeExec();
	LemonScriptRuntime& runtime = codeExec.getLemonScriptRuntime();

	const uint64 effectCodeHash = runtime.getInternalLemonRuntime().addString(effectCode);

	CodeExec::FunctionExecData execData;
	execData.mParams.mReturnType = &lemon::PredefinedDataTypes::UINT_8;
	lemon::Runtime::FunctionCallParameters::Parameter& param1 = vectorAdd(execData.mParams.mParams);
	param1.mDataType = &lemon::PredefinedDataTypes::STRING;
	param1.mStorage = effectCodeHash;

	codeExec.executeScriptFunction("Game.triggerCrowdControlEffect", false, &execData);

	const StatusCode result = (StatusCode)execData.mReturnValueStorage;
	return result;
}
