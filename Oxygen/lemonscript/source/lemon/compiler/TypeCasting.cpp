/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2022 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/compiler/TypeCasting.h"
#include "lemon/compiler/Definitions.h"
#include "lemon/compiler/Utility.h"
#include "lemon/program/DataType.h"


namespace lemon
{

	uint8 TypeCasting::getImplicitCastPriority(const DataTypeDefinition* original, const DataTypeDefinition* target)
	{
		if (original == target)
		{
			// No cast required at all
			return 0;
		}

		// Treat string type as u64, but only for the original type (to allow for converting a string to an integer, but not the other way round)
		//  -> We make an exception for script feature level 1, for the sake of mod compatibility
		if (original == &PredefinedDataTypes::STRING)
			original = &PredefinedDataTypes::UINT_64;
		if (mConfig.mScriptFeatureLevel < 2)
		{
			if (target == &PredefinedDataTypes::STRING)
				target = &PredefinedDataTypes::UINT_64;
		}

		if (original == target)
		{
			// It's a conversion between string and u64
			return 1;
		}

		if (original->getClass() == DataTypeDefinition::Class::INTEGER && target->getClass() == DataTypeDefinition::Class::INTEGER)
		{
			const IntegerDataType& originalInt = original->as<IntegerDataType>();
			const IntegerDataType& targetInt = target->as<IntegerDataType>();

			// Is one type undefined?
			if (originalInt.mSemantics == IntegerDataType::Semantics::CONSTANT)
			{
				// Const may get cast to everything
				return 1;
			}
			if (targetInt.mSemantics == IntegerDataType::Semantics::CONSTANT)
			{
				// Can this happen at all?
				return 1;
			}

			if (originalInt.getBytes() == targetInt.getBytes())
			{
				return (originalInt.mIsSigned && !targetInt.mIsSigned) ? 0x02 : 0x01;
			}

			if (originalInt.getBytes() < targetInt.getBytes())
			{
				// Up cast
				return ((originalInt.mIsSigned && !targetInt.mIsSigned) ? 0x20 : 0x10) + (targetInt.mSizeBits - originalInt.mSizeBits);
			}
			else
			{
				// Down cast
				return ((originalInt.mIsSigned && !targetInt.mIsSigned) ? 0x40 : 0x30) + (originalInt.mSizeBits - targetInt.mSizeBits);
			}
		}
		else
		{
			// No cast possible
			return CANNOT_CAST;
		}
	}

	BaseCastType TypeCasting::getBaseCastType(const DataTypeDefinition* original, const DataTypeDefinition* target)
	{
		if (original == target)
			return BaseCastType::NONE;

		// Treat string type as u64, but only for the original type (to allow for converting a string to an integer, but not the other way round)
		//  -> We make an exception for script feature level 1, for the sake of mod compatibility
		if (original == &PredefinedDataTypes::STRING)
			original = &PredefinedDataTypes::UINT_64;
		if (mConfig.mScriptFeatureLevel < 2)
		{
			if (target == &PredefinedDataTypes::STRING)
				target = &PredefinedDataTypes::UINT_64;
		}

		if (original->getClass() == DataTypeDefinition::Class::INTEGER && target->getClass() == DataTypeDefinition::Class::INTEGER)
		{
			const IntegerDataType& originalInt = original->as<IntegerDataType>();
			const IntegerDataType& targetInt = target->as<IntegerDataType>();

			// No need for an opcode if size does not change at all
			if (originalInt.getBytes() != targetInt.getBytes())
			{
				uint8 castTypeBits = (originalInt.mSizeBits << 2) + targetInt.mSizeBits;
				if (originalInt.mIsSigned && targetInt.getBytes() > originalInt.getBytes())		// Recognize signed up-cast
				{
					castTypeBits += 0x10;
				}
				return (BaseCastType)castTypeBits;
			}
			else
			{
				return BaseCastType::NONE;	// No cast needed
			}
		}
		else
		{
			return BaseCastType::INVALID;
		}
	}

	uint16 TypeCasting::getPriorityOfSignature(const BinaryOperatorSignature& signature, const DataTypeDefinition* left, const DataTypeDefinition* right)
	{
		const uint8 prioLeft = getImplicitCastPriority(left, signature.mLeft);
		const uint8 prioRight = getImplicitCastPriority(right, signature.mRight);
		if (prioLeft < prioRight)
		{
			return ((uint16)prioRight << 8) + (uint16)prioLeft;
		}
		else
		{
			return ((uint16)prioLeft << 8) + (uint16)prioRight;
		}
	}

	uint32 TypeCasting::getPriorityOfSignature(const std::vector<const DataTypeDefinition*>& original, const Function::ParameterList& target)
	{
		if (original.size() != target.size())
			return 0xffffffff;

		const size_t size = original.size();
		static std::vector<uint8> priorities;	// Not multi-threading safe
		priorities.resize(size);

		for (size_t i = 0; i < size; ++i)
		{
			priorities[i] = getImplicitCastPriority(original[i], target[i].mDataType);
		}

		// Highest priority should be first
		std::sort(priorities.begin(), priorities.end(), std::greater<uint8>());

		uint32 result = 0;
		for (size_t i = 0; i < std::min<size_t>(size, 4); ++i)
		{
			result |= (priorities[i] << (24 - i * 8));
		}
		return result;
	}

	bool TypeCasting::getBestSignature(Operator op, const DataTypeDefinition* left, const DataTypeDefinition* right, const BinaryOperatorSignature** outSignature)
	{
		static const std::vector<BinaryOperatorSignature> signaturesSymmetric =
		{
			// TODO: This is oversimplified, there are cases like multiply and left-shift (and probably also add / subtract) that require different handling
			BinaryOperatorSignature(&PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64),
			BinaryOperatorSignature(&PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32),
			BinaryOperatorSignature(&PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16),
			BinaryOperatorSignature(&PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8),
			BinaryOperatorSignature(&PredefinedDataTypes::STRING,  &PredefinedDataTypes::STRING,  &PredefinedDataTypes::STRING)	// TODO: Strings need their own binary operations (and only few of them make actual sense...)
		};
		static const std::vector<BinaryOperatorSignature> signaturesComparison =
		{
			// Result types are always bool
			BinaryOperatorSignature(&PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64,  &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64, &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32,  &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32, &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16,  &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16, &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8,   &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::BOOL),
			BinaryOperatorSignature(&PredefinedDataTypes::STRING,  &PredefinedDataTypes::STRING,  &PredefinedDataTypes::BOOL)	// TODO: Strings need their own comparison operations
		};
		static const std::vector<BinaryOperatorSignature> signaturesTrinary =
		{
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_64,  &PredefinedDataTypes::INT_64),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_64, &PredefinedDataTypes::UINT_64),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_32,  &PredefinedDataTypes::INT_32),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_32, &PredefinedDataTypes::UINT_32),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_16,  &PredefinedDataTypes::INT_16),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_16, &PredefinedDataTypes::UINT_16),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::INT_8,   &PredefinedDataTypes::INT_8),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::UINT_8,  &PredefinedDataTypes::UINT_8),
			BinaryOperatorSignature(&PredefinedDataTypes::BOOL, &PredefinedDataTypes::STRING,  &PredefinedDataTypes::STRING)
		};

		const std::vector<BinaryOperatorSignature>* signatures = nullptr;
		bool exactMatchLeftRequired = false;

		switch (OperatorHelper::getOperatorType(op))
		{
			case OperatorHelper::OperatorType::ASSIGNMENT:
			{
				signatures = &signaturesSymmetric;
				exactMatchLeftRequired = true;
				break;
			}

			case OperatorHelper::OperatorType::SYMMETRIC:
			{
				signatures = &signaturesSymmetric;
				break;
			}

			case OperatorHelper::OperatorType::COMPARISON:
			{
				signatures = &signaturesComparison;
				break;
			}

			case OperatorHelper::OperatorType::TRINARY:
			{
				signatures = &signaturesTrinary;
				break;
			}

			default:
			{
				// This should never happen
				CHECK_ERROR_NOLINE(false, "Unknown operator type");
			}
		}

		uint16 bestPriority = 0xff00;
		for (const BinaryOperatorSignature& signature : *signatures)
		{
			if (exactMatchLeftRequired)
			{
				if (signature.mLeft != left)
					continue;
			}

			const uint16 priority = getPriorityOfSignature(signature, left, right);
			if (priority < bestPriority)
			{
				bestPriority = priority;
				*outSignature = &signature;
			}
		}
		return (bestPriority != 0xff00);
	}

}
