/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/runtime/ControlFlow.h"
#include "lemon/runtime/Runtime.h"
#include "lemon/runtime/RuntimeFunction.h"


namespace lemon
{

	ControlFlow::ControlFlow(Runtime& runtime) :
		mRuntime(runtime)
	{
	}

	void ControlFlow::reset()
	{
		mCallStack.clear();
		for (size_t k = 0; k < VALUE_STACK_MAX_SIZE; ++k)
		{
			mValueStackBuffer[k] = 0;
		}
		mValueStackStart = &mValueStackBuffer[VALUE_STACK_FIRST_INDEX];
		mValueStackPtr   = &mValueStackBuffer[VALUE_STACK_FIRST_INDEX];

		for (size_t k = 0; k < VAR_STACK_LIMIT; ++k)
		{
			mLocalVariablesBuffer[k] = 0;
		}
		mLocalVariablesSize = 0;
	}

	void ControlFlow::getCallStack(std::vector<Location>& outLocations) const
	{
		outLocations.resize(getCallStack().count);
		for (size_t i = 0; i < getCallStack().count; ++i)
		{
			const State& state = getCallStack()[i];
			if (nullptr != state.mRuntimeFunction)
			{
				outLocations[i].mFunction = state.mRuntimeFunction->mFunction;
				outLocations[i].mProgramCounter = state.mRuntimeFunction->translateFromRuntimeProgramCounter(state.mProgramCounter);
			}
			else
			{
				outLocations[i].mFunction = nullptr;
				outLocations[i].mProgramCounter = 0;
			}
		}
	}

	void ControlFlow::getLastStepLocation(Location& outLocation) const
	{
		if (!mCallStack.empty())
		{
			const State& state = mCallStack.back();
			outLocation.mFunction = state.mRuntimeFunction->mFunction;
			outLocation.mProgramCounter = state.mRuntimeFunction->translateFromRuntimeProgramCounter(state.mProgramCounter);
		}
		else
		{
			outLocation.mFunction = nullptr;
			outLocation.mProgramCounter = 0;
		}
	}

}
