/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2023 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "lemon/pch.h"
#include "lemon/compiler/frontend/TokenProcessing.h"
#include "lemon/compiler/TokenHelper.h"
#include "lemon/compiler/TokenTypes.h"
#include "lemon/compiler/Utility.h"
#include "lemon/program/GlobalsLookup.h"
#include "lemon/runtime/BuiltInFunctions.h"
#include "lemon/runtime/OpcodeExecUtils.h"
#include "lemon/runtime/Runtime.h"


namespace lemon
{

	namespace
	{
		std::string getOperatorNotAllowedErrorMessage(Operator op)
		{
			if (op >= Operator::UNARY_NOT && op <= Operator::UNARY_INCREMENT)
			{
				return std::string("Unary operator ") + OperatorHelper::getOperatorCharacters(op) + " is not allowed here";
			}
			else if (op <= Operator::COLON)
			{
				return std::string("Binary operator ") + OperatorHelper::getOperatorCharacters(op) + " is not allowed here";
			}
			else
			{
				switch (op)
				{
					case Operator::SEMICOLON_SEPARATOR:	return "Semicolon ; is only allowed in for-loops";
					case Operator::COMMA_SEPARATOR:		return "Comma , is not allowed here";
					case Operator::PARENTHESIS_LEFT:	return "Parenthesis ( is not allowed here";
					case Operator::PARENTHESIS_RIGHT:	return "Parenthesis ) is not allowed here";
					case Operator::BRACKET_LEFT:		return "Bracket [ is not allowed here";
					case Operator::BRACKET_RIGHT:		return "Bracket ] is not allowed here";
					default: break;
				}
			}
			return "Operator is not allowed here";
		}

		bool tryReplaceConstantsUnary(const ConstantToken& constRight, Operator op, int64& outValue)
		{
			// TODO: Support float/double as well here
			if (constRight.mDataType->getClass() == DataTypeDefinition::Class::INTEGER)
			{
				switch (op)
				{
					case Operator::BINARY_MINUS:  outValue = -constRight.mValue.get<int64>();				return true;
					case Operator::UNARY_NOT:	  outValue = (constRight.mValue.get<int64>() == 0) ? 1 : 0;	return true;
					case Operator::UNARY_BITNOT:  outValue = ~constRight.mValue.get<int64>();				return true;
					default: break;
				}
			}
			return false;
		}

		bool tryReplaceConstantsBinary(const ConstantToken& constLeft, const ConstantToken& constRight, Operator op, int64& outValue)
		{
			// TODO: Support float/double as well here
			//  -> And possibly also combinations with integers?
			if (constLeft.mDataType->getClass() == DataTypeDefinition::Class::INTEGER && constRight.mDataType->getClass() == DataTypeDefinition::Class::INTEGER)
			{
				switch (op)
				{
					case Operator::BINARY_PLUS:			outValue = constLeft.mValue.get<int64>() + constRight.mValue.get<int64>();	return true;
					case Operator::BINARY_MINUS:		outValue = constLeft.mValue.get<int64>() - constRight.mValue.get<int64>();	return true;
					case Operator::BINARY_MULTIPLY:		outValue = constLeft.mValue.get<int64>() * constRight.mValue.get<int64>();	return true;
					case Operator::BINARY_DIVIDE:		outValue = OpcodeExecUtils::safeDivide(constLeft.mValue.get<int64>(), constRight.mValue.get<int64>());  return true;
					case Operator::BINARY_MODULO:		outValue = OpcodeExecUtils::safeModulo(constLeft.mValue.get<int64>(), constRight.mValue.get<int64>());  return true;
					case Operator::BINARY_SHIFT_LEFT:	outValue = constLeft.mValue.get<int64>() << constRight.mValue.get<int64>();	return true;
					case Operator::BINARY_SHIFT_RIGHT:	outValue = constLeft.mValue.get<int64>() >> constRight.mValue.get<int64>();	return true;
					case Operator::BINARY_AND:			outValue = constLeft.mValue.get<int64>() & constRight.mValue.get<int64>();	return true;
					case Operator::BINARY_OR:			outValue = constLeft.mValue.get<int64>() | constRight.mValue.get<int64>();	return true;
					case Operator::BINARY_XOR:			outValue = constLeft.mValue.get<int64>() ^ constRight.mValue.get<int64>();	return true;
					// TODO: How about support for "Operator::COMPARE_EQUAL" etc?
					default: break;
				}
			}
			return false;
		}

		void fillCachedBuiltInFunctionSingle(TokenProcessing::CachedBuiltinFunction& outCached, const GlobalsLookup& globalsLookup, const BuiltInFunctions::FunctionName& functionName)
		{
			const std::vector<Function*>& functions = globalsLookup.getFunctionsByName(functionName.mHash);
			RMX_ASSERT(!functions.empty(), "Unable to find built-in function '" << functionName.mName << "'");
			RMX_ASSERT(functions.size() == 1, "Multiple definitions for built-in function '" << functionName.mName << "'");
			outCached.mFunctions = functions;
		}

		void fillCachedBuiltInFunctionMultiple(TokenProcessing::CachedBuiltinFunction& outCached, const GlobalsLookup& globalsLookup, const BuiltInFunctions::FunctionName& functionName)
		{
			const std::vector<Function*>& functions = globalsLookup.getFunctionsByName(functionName.mHash);
			RMX_ASSERT(!functions.empty(), "Unable to find built-in function '" << functionName.mName << "'");
			outCached.mFunctions = functions;
		}

		template<typename T>
		T* findInList(const std::vector<T*>& list, uint64 nameHash)
		{
			for (T* item : list)
			{
				if (item->getName().getHash() == nameHash)
					return item;
			}
			return nullptr;
		}
	}


	TokenProcessing::TokenProcessing(GlobalsLookup& globalsLookup, const CompileOptions& compileOptions) :
		mGlobalsLookup(globalsLookup),
		mCompileOptions(compileOptions),
		mTypeCasting(compileOptions)
	{
		fillCachedBuiltInFunctionMultiple(mBuiltinConstantArrayAccess,			globalsLookup, BuiltInFunctions::CONSTANT_ARRAY_ACCESS);
		fillCachedBuiltInFunctionSingle(mBuiltinStringOperatorPlus,				globalsLookup, BuiltInFunctions::STRING_OPERATOR_PLUS);
		fillCachedBuiltInFunctionSingle(mBuiltinStringOperatorPlusInt64,		globalsLookup, BuiltInFunctions::STRING_OPERATOR_PLUS_INT64);
		fillCachedBuiltInFunctionSingle(mBuiltinStringOperatorPlusInt64Inv,		globalsLookup, BuiltInFunctions::STRING_OPERATOR_PLUS_INT64_INV);
		fillCachedBuiltInFunctionSingle(mBuiltinStringOperatorLess,				globalsLookup, BuiltInFunctions::STRING_OPERATOR_LESS);
		fillCachedBuiltInFunctionSingle(mBuiltinStringOperatorLessOrEqual,		globalsLookup, BuiltInFunctions::STRING_OPERATOR_LESS_OR_EQUAL);
		fillCachedBuiltInFunctionSingle(mBuiltinStringOperatorGreater,			globalsLookup, BuiltInFunctions::STRING_OPERATOR_GREATER);
		fillCachedBuiltInFunctionSingle(mBuiltinStringOperatorGreaterOrEqual,	globalsLookup, BuiltInFunctions::STRING_OPERATOR_GREATER_OR_EQUAL);

		mBinaryOperationLookup[(size_t)Operator::BINARY_PLUS]             .emplace_back(&mBuiltinStringOperatorPlus,           &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING);
		mBinaryOperationLookup[(size_t)Operator::BINARY_PLUS]             .emplace_back(&mBuiltinStringOperatorPlusInt64,      &PredefinedDataTypes::STRING, &PredefinedDataTypes::INT_64, &PredefinedDataTypes::STRING);
		mBinaryOperationLookup[(size_t)Operator::BINARY_PLUS]             .emplace_back(&mBuiltinStringOperatorPlusInt64Inv,   &PredefinedDataTypes::INT_64, &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING);
		mBinaryOperationLookup[(size_t)Operator::ASSIGN_PLUS]             .emplace_back(&mBuiltinStringOperatorPlus,           &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING, Operator::BINARY_PLUS);
		mBinaryOperationLookup[(size_t)Operator::ASSIGN_PLUS]             .emplace_back(&mBuiltinStringOperatorPlusInt64,      &PredefinedDataTypes::STRING, &PredefinedDataTypes::INT_64, &PredefinedDataTypes::STRING, Operator::BINARY_PLUS);
		mBinaryOperationLookup[(size_t)Operator::COMPARE_LESS]            .emplace_back(&mBuiltinStringOperatorLess,           &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING, &PredefinedDataTypes::BOOL);
		mBinaryOperationLookup[(size_t)Operator::COMPARE_LESS_OR_EQUAL]   .emplace_back(&mBuiltinStringOperatorLessOrEqual,    &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING, &PredefinedDataTypes::BOOL);
		mBinaryOperationLookup[(size_t)Operator::COMPARE_GREATER]         .emplace_back(&mBuiltinStringOperatorGreater,        &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING, &PredefinedDataTypes::BOOL);
		mBinaryOperationLookup[(size_t)Operator::COMPARE_GREATER_OR_EQUAL].emplace_back(&mBuiltinStringOperatorGreaterOrEqual, &PredefinedDataTypes::STRING, &PredefinedDataTypes::STRING, &PredefinedDataTypes::BOOL);
	}

	void TokenProcessing::processTokens(TokenList& tokensRoot, uint32 lineNumber, const DataTypeDefinition* resultType)
	{
		mLineNumber = lineNumber;

		// Try to resolve identifiers
		resolveIdentifiers(tokensRoot);

		// Process defines early, as they can introduce new tokens that need to be considered in the following steps
		processDefines(tokensRoot);

		// Process constants
		processConstants(tokensRoot);

		// Build hierarchy by processing parentheses
		processParentheses(tokensRoot);

		// Build hierarchy by processing commas (usually those separating parameters in function calls)
		processCommaSeparators(tokensRoot);

		// Recursively go through the hierarchy of tokens for the main part of processing
		processTokenListRecursive(tokensRoot);

		// TODO: Statement type assignment will require resolving all identifiers first -- check if this is done here
		assignStatementDataTypes(tokensRoot, resultType);
	}

	void TokenProcessing::processForPreprocessor(TokenList& tokensRoot, uint32 lineNumber)
	{
		mLineNumber = lineNumber;

		// Build hierarchy by processing parentheses
		processParentheses(tokensRoot);

		// Recursively go through the hierarchy of tokens for the main part of processing
		processTokenListRecursiveForPreprocessor(tokensRoot);
	}

	bool TokenProcessing::resolveIdentifiers(TokenList& tokens)
	{
		bool anyResolved = false;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isA<IdentifierToken>())
			{
				IdentifierToken& identifierToken = tokens[i].as<IdentifierToken>();
				if (nullptr == identifierToken.mResolved)
				{
					const uint64 nameHash = identifierToken.mName.getHash();
					identifierToken.mResolved = mGlobalsLookup.resolveIdentifierByHash(nameHash);
					if (nullptr != identifierToken.mResolved && identifierToken.mResolved->getType() == GlobalsLookup::Identifier::Type::DATA_TYPE)
					{
						VarTypeToken& varTypeToken = tokens.createReplaceAt<VarTypeToken>(i);
						varTypeToken.mDataType = &identifierToken.mResolved->as<const DataTypeDefinition>();
					}
					anyResolved = true;
				}
			}
		}
		return anyResolved;
	}

	void TokenProcessing::insertCastTokenIfNecessary(TokenPtr<StatementToken>& token, const DataTypeDefinition* targetDataType)
	{
		const TypeCasting::CastHandling castHandling = TypeCasting(mCompileOptions).getCastHandling(token->mDataType, targetDataType, false);
		if (castHandling.mResult == TypeCasting::CastHandling::Result::BASE_CAST)
		{
			TokenPtr<StatementToken> inner = token;		// Make a copy, as the original gets replaced in the next line
			ValueCastToken& vct = token.create<ValueCastToken>();
			vct.mDataType = targetDataType;
			vct.mArgument = inner;
		}
	}

	void TokenProcessing::castCompileTimeConstant(ConstantToken& constantToken, const DataTypeDefinition* targetDataType)
	{
		const TypeCasting::CastHandling castHandling = TypeCasting(mCompileOptions).getCastHandling(constantToken.mDataType, targetDataType, false);
		switch (castHandling.mResult)
		{
			case TypeCasting::CastHandling::Result::NO_CAST:
			{
				// No cast needed
				break;
			}

			case TypeCasting::CastHandling::Result::BASE_CAST:
			{
				switch (castHandling.mBaseCastType)
				{
					// Cast down (signed or unsigned makes no difference here)
					case BaseCastType::INT_16_TO_8:  constantToken.mValue.cast<uint16, uint8 >();  break;
					case BaseCastType::INT_32_TO_8:  constantToken.mValue.cast<uint32, uint8 >();  break;
					case BaseCastType::INT_64_TO_8:  constantToken.mValue.cast<uint64, uint8 >();  break;
					case BaseCastType::INT_32_TO_16: constantToken.mValue.cast<uint32, uint16>();  break;
					case BaseCastType::INT_64_TO_16: constantToken.mValue.cast<uint64, uint16>();  break;
					case BaseCastType::INT_64_TO_32: constantToken.mValue.cast<uint64, uint32>();  break;

					// Cast up (value is unsigned -> adding zeroes)
					case BaseCastType::UINT_8_TO_16:  constantToken.mValue.cast<uint8,  uint16>();  break;
					case BaseCastType::UINT_8_TO_32:  constantToken.mValue.cast<uint8,  uint32>();  break;
					case BaseCastType::UINT_8_TO_64:  constantToken.mValue.cast<uint8,  uint64>();  break;
					case BaseCastType::UINT_16_TO_32: constantToken.mValue.cast<uint16, uint32>();  break;
					case BaseCastType::UINT_16_TO_64: constantToken.mValue.cast<uint16, uint64>();  break;
					case BaseCastType::UINT_32_TO_64: constantToken.mValue.cast<uint32, uint64>();  break;

					// Cast up (value is signed -> adding highest bit)
					case BaseCastType::SINT_8_TO_16:  constantToken.mValue.cast<int8,  int16>();  break;
					case BaseCastType::SINT_8_TO_32:  constantToken.mValue.cast<int8,  int32>();  break;
					case BaseCastType::SINT_8_TO_64:  constantToken.mValue.cast<int8,  int64>();  break;
					case BaseCastType::SINT_16_TO_32: constantToken.mValue.cast<int16, int32>();  break;
					case BaseCastType::SINT_16_TO_64: constantToken.mValue.cast<int16, int64>();  break;
					case BaseCastType::SINT_32_TO_64: constantToken.mValue.cast<int32, int64>();  break;

					// Integer cast to float
					case BaseCastType::UINT_8_TO_FLOAT:   constantToken.mValue.cast<uint8,  float>();  break;
					case BaseCastType::UINT_16_TO_FLOAT:  constantToken.mValue.cast<uint16, float>();  break;
					case BaseCastType::UINT_32_TO_FLOAT:  constantToken.mValue.cast<uint32, float>();  break;
					case BaseCastType::UINT_64_TO_FLOAT:  constantToken.mValue.cast<uint64, float>();  break;
					case BaseCastType::SINT_8_TO_FLOAT:   constantToken.mValue.cast<int8,   float>();  break;
					case BaseCastType::SINT_16_TO_FLOAT:  constantToken.mValue.cast<int16,  float>();  break;
					case BaseCastType::SINT_32_TO_FLOAT:  constantToken.mValue.cast<int32,  float>();  break;
					case BaseCastType::SINT_64_TO_FLOAT:  constantToken.mValue.cast<int64,  float>();  break;

					case BaseCastType::UINT_8_TO_DOUBLE:  constantToken.mValue.cast<uint8,  double>();  break;
					case BaseCastType::UINT_16_TO_DOUBLE: constantToken.mValue.cast<uint16, double>();  break;
					case BaseCastType::UINT_32_TO_DOUBLE: constantToken.mValue.cast<uint32, double>();  break;
					case BaseCastType::UINT_64_TO_DOUBLE: constantToken.mValue.cast<uint64, double>();  break;
					case BaseCastType::SINT_8_TO_DOUBLE:  constantToken.mValue.cast<int8,   double>();  break;
					case BaseCastType::SINT_16_TO_DOUBLE: constantToken.mValue.cast<int16,  double>();  break;
					case BaseCastType::SINT_32_TO_DOUBLE: constantToken.mValue.cast<int32,  double>();  break;
					case BaseCastType::SINT_64_TO_DOUBLE: constantToken.mValue.cast<int64,  double>();  break;

					// Float cast to integer
					case BaseCastType::FLOAT_TO_UINT_8:   constantToken.mValue.cast<float, uint8 >();  break;
					case BaseCastType::FLOAT_TO_UINT_16:  constantToken.mValue.cast<float, uint16>();  break;
					case BaseCastType::FLOAT_TO_UINT_32:  constantToken.mValue.cast<float, uint32>();  break;
					case BaseCastType::FLOAT_TO_UINT_64:  constantToken.mValue.cast<float, uint64>();  break;
					case BaseCastType::FLOAT_TO_SINT_8:   constantToken.mValue.cast<float, int8  >();  break;
					case BaseCastType::FLOAT_TO_SINT_16:  constantToken.mValue.cast<float, int16 >();  break;
					case BaseCastType::FLOAT_TO_SINT_32:  constantToken.mValue.cast<float, int32 >();  break;
					case BaseCastType::FLOAT_TO_SINT_64:  constantToken.mValue.cast<float, int64 >();  break;

					case BaseCastType::DOUBLE_TO_UINT_8:  constantToken.mValue.cast<double, uint8 >();  break;
					case BaseCastType::DOUBLE_TO_UINT_16: constantToken.mValue.cast<double, uint16>();  break;
					case BaseCastType::DOUBLE_TO_UINT_32: constantToken.mValue.cast<double, uint32>();  break;
					case BaseCastType::DOUBLE_TO_UINT_64: constantToken.mValue.cast<double, uint64>();  break;
					case BaseCastType::DOUBLE_TO_SINT_8:  constantToken.mValue.cast<double, int8  >();  break;
					case BaseCastType::DOUBLE_TO_SINT_16: constantToken.mValue.cast<double, int16 >();  break;
					case BaseCastType::DOUBLE_TO_SINT_32: constantToken.mValue.cast<double, int32 >();  break;
					case BaseCastType::DOUBLE_TO_SINT_64: constantToken.mValue.cast<double, int64 >();  break;

					// Float cast
					case BaseCastType::FLOAT_TO_DOUBLE:   constantToken.mValue.cast<float, double>();  break;
					case BaseCastType::DOUBLE_TO_FLOAT:   constantToken.mValue.cast<double, float>();  break;

					default:
						throw std::runtime_error("Unrecognized cast type");
				}
				break;
			}

			case TypeCasting::CastHandling::Result::ANY_CAST:
			{
				// Anything to do here...?
				break;
			}

			default:
			case TypeCasting::CastHandling::Result::INVALID:
				CHECK_ERROR(false, "Invalid cast of constants", mLineNumber);
		}
	}

	void TokenProcessing::processDefines(TokenList& tokens)
	{
		bool anyDefineResolved = false;
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isA<IdentifierToken>())
			{
				IdentifierToken& identifierToken = tokens[i].as<IdentifierToken>();
				if (nullptr != identifierToken.mResolved && identifierToken.mResolved->getType() == GlobalsLookup::Identifier::Type::DEFINE)
				{
					const Define& define = identifierToken.mResolved->as<Define>();
					tokens.erase(i);
					for (size_t k = 0; k < define.mContent.size(); ++k)
					{
						tokens.insert(define.mContent[k], i + k);
					}

					// TODO: Add implicit cast if necessary

					anyDefineResolved = true;
				}
			}
		}

		if (anyDefineResolved)
		{
			resolveIdentifiers(tokens);
		}
	}

	void TokenProcessing::processConstants(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isA<IdentifierToken>())
			{
				IdentifierToken& identifierToken = tokens[i].as<IdentifierToken>();
				const Constant* constant = nullptr;
				if (nullptr != identifierToken.mResolved && identifierToken.mResolved->getType() == GlobalsLookup::Identifier::Type::CONSTANT)
				{
					constant = &identifierToken.mResolved->as<Constant>();
				}
				else
				{
					for (const Constant& localConstant : *mContext.mLocalConstants)
					{
						if (localConstant.getName() == identifierToken.mName)
						{
							constant = &localConstant;
							break;
						}
					}
					if (nullptr == constant)
						continue;
				}

				ConstantToken& newToken = tokens.createReplaceAt<ConstantToken>(i);
				newToken.mDataType = constant->getDataType();
				newToken.mValue.set(constant->getValue());
			}
		}
	}

	void TokenProcessing::processParentheses(TokenList& tokens)
	{
		static std::vector<std::pair<ParenthesisType, size_t>> parenthesisStack;	// Not multi-threading safe
		parenthesisStack.clear();
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isA<OperatorToken>())
			{
				const OperatorToken& opToken = tokens[i].as<OperatorToken>();
				if (opToken.mOperator == Operator::PARENTHESIS_LEFT ||
					opToken.mOperator == Operator::BRACKET_LEFT)
				{
					const ParenthesisType type = (opToken.mOperator == Operator::PARENTHESIS_LEFT) ? ParenthesisType::PARENTHESIS : ParenthesisType::BRACKET;
					parenthesisStack.emplace_back(type, i);
				}
				else if (opToken.mOperator == Operator::PARENTHESIS_RIGHT ||
						 opToken.mOperator == Operator::BRACKET_RIGHT)
				{
					const ParenthesisType type = (opToken.mOperator == Operator::PARENTHESIS_RIGHT) ? ParenthesisType::PARENTHESIS : ParenthesisType::BRACKET;
					CHECK_ERROR(!parenthesisStack.empty() && parenthesisStack.back().first == type, "Parenthesis not matching (too many closed)", mLineNumber);

					// Pack all between parentheses into a new token
					const size_t startPosition = parenthesisStack.back().second;
					const size_t endPosition = i;
					const bool isEmpty = (endPosition == startPosition + 1);

					parenthesisStack.pop_back();

					// Left parenthesis will be replaced with a parenthesis token representing the whole thing
					ParenthesisToken& token = tokens.createReplaceAt<ParenthesisToken>(startPosition);
					token.mParenthesisType = type;

					// Right parenthesis just gets removed
					tokens.erase(endPosition);

					if (!isEmpty)
					{
						// Copy content as new token list into the parenthesis token
						token.mContent.moveFrom(tokens, startPosition + 1, endPosition - startPosition - 1);
					}

					i -= (endPosition - startPosition);
				}
			}
		}

		CHECK_ERROR(parenthesisStack.empty(), "Parenthesis not matching (too many open)", mLineNumber);
	}

	void TokenProcessing::processCommaSeparators(TokenList& tokens)
	{
		// Recursively go through the whole parenthesis hierarchy
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isA<ParenthesisToken>())
			{
				// Call recursively for this parenthesis
				processCommaSeparators(tokens[i].as<ParenthesisToken>().mContent);
			}
		}

		// Find comma positions
		static std::vector<size_t> commaPositions;	// Not multi-threading safe
		commaPositions.clear();
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			Token& token = tokens[i];
			if (isOperator(token, Operator::COMMA_SEPARATOR))
			{
				commaPositions.push_back(i);
			}
		}

		// Any commas?
		if (!commaPositions.empty())
		{
			CommaSeparatedListToken& commaSeparatedListToken = tokens.createFront<CommaSeparatedListToken>();
			commaSeparatedListToken.mContent.resize(commaPositions.size() + 1);

			// All comma positions have changed by 1
			for (size_t& pos : commaPositions)
				++pos;

			// Add "virtual" comma at the front for symmetry reasons
			commaPositions.insert(commaPositions.begin(), 0);

			for (int j = (int)commaPositions.size() - 1; j >= 0; --j)
			{
				const size_t first = commaPositions[j] + 1;
				commaSeparatedListToken.mContent[j].moveFrom(tokens, first, tokens.size() - first);

				if (j > 0)
				{
					// Erase the comma token itself
					CHECK_ERROR(isOperator(tokens[commaPositions[j]], Operator::COMMA_SEPARATOR), "Wrong token index", mLineNumber);
					tokens.erase(commaPositions[j]);
				}
			}
			CHECK_ERROR(tokens.size() == 1, "Token list must only contain the CommaSeparatedListToken afterwards", mLineNumber);
		}
	}

	void TokenProcessing::processTokenListRecursive(TokenList& tokens)
	{
		// Resolve occurrences of "addressof" that refer to functions
		//  -> These need to be resolved before processing the child tokens, because the function name as a sole identifier would cause a syntax error
		resolveAddressOfFunctions(tokens);

		// Go through the child token lists
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			switch (tokens[i].getType())
			{
				case Token::Type::PARENTHESIS:
				{
					// Call recursively for this parenthesis' contents
					processTokenListRecursive(tokens[i].as<ParenthesisToken>().mContent);
					break;
				}

				case Token::Type::COMMA_SEPARATED:
				{
					// Call recursively for each comma-separated part
					for (TokenList& content : tokens[i].as<CommaSeparatedListToken>().mContent)
					{
						processTokenListRecursive(content);
					}
					break;
				}

                default:
                    break;
			}
		}

		// Now for the other processing steps, which are done after processing the child tokens
		processVariableDefinitions(tokens);
		processFunctionCalls(tokens);
		processMemoryAccesses(tokens);
		processArrayAccesses(tokens);
		processExplicitCasts(tokens);
		processVariables(tokens);

		resolveAddressOfMemoryAccesses(tokens);

		processUnaryOperations(tokens);
		processBinaryOperations(tokens);

		evaluateCompileTimeConstants(tokens);
	}

	void TokenProcessing::processTokenListRecursiveForPreprocessor(TokenList& tokens)
	{
		// Go through the child token lists
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isA<ParenthesisToken>())
			{
				// Call recursively for this parenthesis' contents
				processTokenListRecursiveForPreprocessor(tokens[i].as<ParenthesisToken>().mContent);
			}
		}

		// Now for the other processing steps
		processUnaryOperations(tokens);
		processBinaryOperations(tokens);
	}

	void TokenProcessing::processVariableDefinitions(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			Token& token = tokens[i];
			switch (token.getType())
			{
				case Token::Type::KEYWORD:
				{
					const Keyword keyword = token.as<KeywordToken>().mKeyword;
					if (keyword == Keyword::FUNCTION)
					{
						// Next token must be an identifier
						CHECK_ERROR(i+1 < tokens.size() && tokens[i+1].isA<IdentifierToken>(), "Function keyword must be followed by an identifier", mLineNumber);

						// TODO: We could register the function name here already, so it is known later on...

					}
					break;
				}

				case Token::Type::VARTYPE:
				{
					const DataTypeDefinition* varType = token.as<VarTypeToken>().mDataType;

					// Next token must be an identifier
					CHECK_ERROR(i+1 < tokens.size(), "Type name must not be the last token", mLineNumber);

					// Next token must be an identifier
					Token& nextToken = tokens[i+1];
					if (nextToken.isA<IdentifierToken>())
					{
						CHECK_ERROR(varType->getClass() != DataTypeDefinition::Class::VOID, "void variables not allowed", mLineNumber);

						// Create new variable
						const IdentifierToken& identifierToken = tokens[i+1].as<IdentifierToken>();
						CHECK_ERROR(nullptr == findLocalVariable(identifierToken.mName.getHash()), "Variable name '" << identifierToken.mName.getString() << "' already used", mLineNumber);

						// Variable may already exist in function (but not in scope, we just checked that)
						RMX_ASSERT(nullptr != mContext.mFunction, "Invalid function pointer");
						LocalVariable* variable = mContext.mFunction->getLocalVariableByIdentifier(identifierToken.mName.getHash());
						if (nullptr == variable)
						{
							variable = &mContext.mFunction->addLocalVariable(identifierToken.mName, varType, mLineNumber);
						}
						mContext.mLocalVariables->push_back(variable);

						VariableToken& variableToken = tokens.createReplaceAt<VariableToken>(i);
						variableToken.mVariable = variable;
						variableToken.mDataType = variable->getDataType();

						tokens.erase(i+1);
					}
					break;
				}

				default:
					break;
			}
		}
	}

	void TokenProcessing::processFunctionCalls(TokenList& tokens)
	{
		for (size_t i = 0; i + 1 < tokens.size(); ++i)
		{
			if (tokens[i].isA<IdentifierToken>() && isParenthesis(tokens[i+1], ParenthesisType::PARENTHESIS))
			{
				TokenList& content = tokens[i+1].as<ParenthesisToken>().mContent;
				IdentifierToken& identifierToken = tokens[i].as<IdentifierToken>();
				const std::string_view functionName = identifierToken.mName.getString();
				bool isBaseCall = false;
				bool baseFunctionExists = false;
				const Function* function = nullptr;
				const Variable* thisPointerVariable = nullptr;

				const std::vector<Function*>* candidateFunctions = &mGlobalsLookup.getFunctionsByName(identifierToken.mName.getHash());
				if (!candidateFunctions->empty())
				{
					// Is it a global function
				}
				else if (rmx::startsWith(functionName, "base."))
				{
					// It's a base call
					RMX_ASSERT(nullptr != mContext.mFunction, "Invalid function pointer");
					CHECK_ERROR(functionName.substr(5) == mContext.mFunction->getName().getString(), "Base call '" << functionName << "' goes to a different function, expected 'base." << mContext.mFunction->getName() << "' instead", mLineNumber);
					isBaseCall = true;

					const std::string_view baseName = identifierToken.mName.getString().substr(5);
					const std::vector<Function*>& candidates = mGlobalsLookup.getFunctionsByName(rmx::getMurmur2_64(baseName));
					for (Function* candidate : candidates)
					{
						// Base function signature must be the same as current function's
						if (candidate->getSignatureHash() == mContext.mFunction->getSignatureHash() && candidate != mContext.mFunction)
						{
							baseFunctionExists = true;
							break;
						}
					}

					// TODO: The following check would be no good idea, as some mods overwrite functions (and call their base) from other mods that may or may not be loaded before
					//  -> The solution is to allow this, and make the base calls simply do nothing at all
					//CHECK_ERROR(baseFunctionExists, "There's no base function for call '" << functionName << "' with the same signature, i.e. exact same types for parameters and return value", mLineNumber);
				}
				else
				{
					bool isValidFunctionCall = false;

					const size_t lastDot = functionName.find_last_of('.');
					if (lastDot != std::string_view::npos)
					{
						const std::string_view contextPart = functionName.substr(0, lastDot);
						const std::string_view namePart = functionName.substr(lastDot+1);

						// Check for a method-like function call
						//  -> First part must be an identifier of a variable in that case
						thisPointerVariable = findVariable(rmx::getMurmur2_64(contextPart));
						if (nullptr != thisPointerVariable)
						{
							candidateFunctions = &mGlobalsLookup.getMethodsByName(thisPointerVariable->getDataType()->getName().getHash() + rmx::getMurmur2_64(namePart));
							isValidFunctionCall = !candidateFunctions->empty();
						}

						if (!isValidFunctionCall)
						{
							// Special handling for "array.length()"
							//  -> TODO: Unify this with the method-like function call stuff above
							if (namePart == "length" && content.empty())
							{
								const ConstantArray* constantArray = findConstantArray(rmx::getMurmur2_64(contextPart));
								if (nullptr != constantArray)
								{
									// This can simply be replaced with a compile-time constant
									ConstantToken& constantToken = tokens.createReplaceAt<ConstantToken>(i);
									constantToken.mValue.set<uint64>(constantArray->getSize());
									constantToken.mDataType = &PredefinedDataTypes::CONST_INT;
									tokens.erase(i+1);
									continue;
								}
							}
						}
					}

					CHECK_ERROR(isValidFunctionCall, "Unknown function name '" << functionName << "'", mLineNumber);
				}

				// Create function token
				FunctionToken& functionToken = tokens.createReplaceAt<FunctionToken>(i);

				// Build list of parameters
				if (!content.empty())
				{
					if (content[0].isA<CommaSeparatedListToken>())
					{
						const std::vector<TokenList>& tokenLists = content[0].as<CommaSeparatedListToken>().mContent;
						functionToken.mParameters.reserve(tokenLists.size());
						for (const TokenList& tokenList : tokenLists)
						{
							CHECK_ERROR(tokenList.size() == 1, "Function parameter content must be one token", mLineNumber);
							CHECK_ERROR(tokenList[0].isStatement(), "Function parameter content must be a statement", mLineNumber);
							vectorAdd(functionToken.mParameters) = tokenList[0].as<StatementToken>();
						}
					}
					else
					{
						CHECK_ERROR(content.size() == 1, "Function parameter content must be one token", mLineNumber);
						CHECK_ERROR(content[0].isStatement(), "Function parameter content must be a statement", mLineNumber);
						vectorAdd(functionToken.mParameters) = content[0].as<StatementToken>();
					}
				}
				if (nullptr != thisPointerVariable)
				{
					// Add as implicit first parameter
					const auto it = functionToken.mParameters.emplace(functionToken.mParameters.begin());
					VariableToken& variableToken = it->create<VariableToken>();
					variableToken.mVariable = thisPointerVariable;
					variableToken.mDataType = thisPointerVariable->getDataType();
				}
				tokens.erase(i+1);

				// Assign types
				static std::vector<const DataTypeDefinition*> parameterTypes;	// Not multi-threading safe
				parameterTypes.resize(functionToken.mParameters.size());
				for (size_t k = 0; k < functionToken.mParameters.size(); ++k)
				{
					parameterTypes[k] = assignStatementDataType(*functionToken.mParameters[k], nullptr);
				}

				// If the function was not determined yet, do that now
				if (nullptr == function)
				{
					// Find out which function signature actually fits
					RMX_ASSERT(nullptr != mContext.mFunction, "Invalid function pointer");
					if (isBaseCall)
					{
						// Base call must use the same function signature as the current one
						CHECK_ERROR(parameterTypes.size() == mContext.mFunction->getParameters().size(), "Base function call for '" << functionName << "' has different parameter count", mLineNumber);
						size_t failedIndex = 0;
						const bool canMatch = mTypeCasting.canMatchSignature(parameterTypes, mContext.mFunction->getParameters(), &failedIndex);
						CHECK_ERROR(canMatch, "Can't cast parameters of '" << functionName << "' function call to match base function, parameter '" << mContext.mFunction->getParameters()[failedIndex].mName << "' has the wrong type", mLineNumber);

						if (baseFunctionExists)
						{
							// Use the very same function again, as a base call
							function = mContext.mFunction;
							functionToken.mIsBaseCall = true;
						}
						else
						{
							// Base call would go nowhere - better replace the token again with one doing nothing at all, or returning a default value
							const DataTypeDefinition* returnType = mContext.mFunction->getReturnType();
							switch (returnType->getClass())
							{
								case DataTypeDefinition::Class::VOID:
								{
									tokens.erase(i);
									break;
								}
								case DataTypeDefinition::Class::INTEGER:
								case DataTypeDefinition::Class::STRING:
								{
									ConstantToken& constantToken = tokens.createReplaceAt<ConstantToken>(i);
									constantToken.mValue.reset();
									constantToken.mDataType = returnType;
									break;
								}
								case DataTypeDefinition::Class::ANY:
								{
									CHECK_ERROR(canMatch, "'any' type cannot be used as a return value", mLineNumber);
									break;
								}
								default:
									break;
							}
							return;
						}
					}
					else
					{
						// Find best-fitting correct function overload
						function = nullptr;
						uint32 bestPriority = 0xff000000;
						for (const Function* candidateFunction : *candidateFunctions)
						{
							const uint32 priority = mTypeCasting.getPriorityOfSignature(parameterTypes, candidateFunction->getParameters());
							if (priority < bestPriority)
							{
								bestPriority = priority;
								function = candidateFunction;
							}
						}
						CHECK_ERROR(bestPriority < 0xff000000, "No appropriate function overload found calling '" << functionName << "', the number or types of parameters passed are wrong", mLineNumber);
					}
				}

				if (nullptr != function)
				{
					functionToken.mFunction = function;
					functionToken.mDataType = function->getReturnType();
				}
			}
		}
	}

	void TokenProcessing::processMemoryAccesses(TokenList& tokens)
	{
		for (size_t i = 0; i + 1 < tokens.size(); ++i)
		{
			if (tokens[i].isA<VarTypeToken>() && isParenthesis(tokens[i+1], ParenthesisType::BRACKET))
			{
				TokenList& content = tokens[i+1].as<ParenthesisToken>().mContent;
				CHECK_ERROR(content.size() == 1, "Expected exactly one token inside brackets", mLineNumber);
				CHECK_ERROR(content[0].isStatement(), "Expected statement token inside brackets", mLineNumber);

				const DataTypeDefinition* dataType = tokens[i].as<VarTypeToken>().mDataType;
				CHECK_ERROR(dataType->getClass() == DataTypeDefinition::Class::INTEGER && dataType->as<IntegerDataType>().mSemantics == IntegerDataType::Semantics::DEFAULT, "Memory access is only possible using basic integer types, but not '" << dataType->getName() << "'", mLineNumber);

				MemoryAccessToken& token = tokens.createReplaceAt<MemoryAccessToken>(i);
				token.mDataType = dataType;
				token.mAddress = content[0].as<StatementToken>();
				tokens.erase(i+1);

				assignStatementDataType(*token.mAddress, &PredefinedDataTypes::UINT_32);
			}
		}
	}

	void TokenProcessing::processArrayAccesses(TokenList& tokens)
	{
		for (size_t i = 0; i + 1 < tokens.size(); ++i)
		{
			if (tokens[i].isA<IdentifierToken>() && isParenthesis(tokens[i+1], ParenthesisType::BRACKET))
			{
				// Check the identifier
				IdentifierToken& identifierToken = tokens[i].as<IdentifierToken>();
				const ConstantArray* constantArray = nullptr;
				if (nullptr != identifierToken.mResolved && identifierToken.mResolved->getType() == GlobalsLookup::Identifier::Type::CONSTANT_ARRAY)
				{
					constantArray = &identifierToken.mResolved->as<ConstantArray>();
				}
				else
				{
					// Check for local constant array
					constantArray = findInList(*mContext.mLocalConstantArrays, identifierToken.mName.getHash());
					CHECK_ERROR(nullptr != constantArray, "Unable to resolve identifier: " << identifierToken.mName.getString(), mLineNumber);
				}

				TokenList& content = tokens[i+1].as<ParenthesisToken>().mContent;
				CHECK_ERROR(content.size() == 1, "Expected exactly one token inside brackets", mLineNumber);
				CHECK_ERROR(content[0].isStatement(), "Expected statement token inside brackets", mLineNumber);

				const Function* matchingFunction = nullptr;
				for (const Function* function : mBuiltinConstantArrayAccess.mFunctions)
				{
					if (function->getReturnType() == constantArray->getElementDataType())
					{
						matchingFunction = function;
						break;
					}
				}
				if (nullptr == matchingFunction)
					continue;

			#ifdef DEBUG
				const Function::ParameterList& parameterList = matchingFunction->getParameters();
				RMX_ASSERT(parameterList.size() == 2 && parameterList[0].mDataType == &PredefinedDataTypes::UINT_32 && parameterList[1].mDataType == &PredefinedDataTypes::UINT_32, "Function signature for constant array access does not fit");
			#endif

				FunctionToken& token = tokens.createReplaceAt<FunctionToken>(i);
				token.mFunction = matchingFunction;
				token.mParameters.resize(2);
				ConstantToken& idToken = token.mParameters[0].create<ConstantToken>();
				idToken.mValue.set(constantArray->getID());
				idToken.mDataType = &PredefinedDataTypes::UINT_32;
				token.mParameters[1] = content[0].as<StatementToken>();		// Array index
				token.mDataType = matchingFunction->getReturnType();

				assignStatementDataType(*token.mParameters[0], matchingFunction->getParameters()[0].mDataType);
				assignStatementDataType(*token.mParameters[1], matchingFunction->getParameters()[1].mDataType);

				tokens.erase(i+1);
			}
		}
	}

	void TokenProcessing::processExplicitCasts(TokenList& tokens)
	{
		for (size_t i = 0; i + 1 < tokens.size(); ++i)
		{
			if (tokens[i].isA<VarTypeToken>() && isParenthesis(tokens[i+1], ParenthesisType::PARENTHESIS))
			{
				const DataTypeDefinition* targetType = tokens[i].as<VarTypeToken>().mDataType;

				ValueCastToken& token = tokens.createReplaceAt<ValueCastToken>(i);
				token.mArgument = tokens[i+1].as<ParenthesisToken>();
				token.mDataType = targetType;
				tokens.erase(i+1);
			}
		}
	}

	void TokenProcessing::processVariables(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			Token& token = tokens[i];
			if (token.isA<IdentifierToken>())
			{
				// Check the identifier
				IdentifierToken& identifierToken = tokens[i].as<IdentifierToken>();
				const Variable* variable = nullptr;
				if (nullptr != identifierToken.mResolved && identifierToken.mResolved->getType() == GlobalsLookup::Identifier::Type::VARIABLE)
				{
					variable = &identifierToken.mResolved->as<Variable>();
				}
				else
				{
					// Check for local variable
					variable = findLocalVariable(identifierToken.mName.getHash());
					CHECK_ERROR(nullptr != variable, "Unable to resolve identifier: " << identifierToken.mName.getString(), mLineNumber);
				}

				VariableToken& variableToken = tokens.createReplaceAt<VariableToken>(i);
				variableToken.mVariable = variable;
				variableToken.mDataType = variable->getDataType();
			}
		}
	}

	void TokenProcessing::processUnaryOperations(TokenList& tokens)
	{
		// Left to right associative
		for (int i = 0; i < (int)tokens.size(); ++i)
		{
			if (tokens[i].isA<OperatorToken>())
			{
				const Operator op = tokens[i].as<OperatorToken>().mOperator;
				switch (op)
				{
					case Operator::UNARY_DECREMENT:
					case Operator::UNARY_INCREMENT:
					{
						// Postfix
						if (i == 0)
							continue;

						Token& leftToken = tokens[i-1];
						if (!leftToken.isStatement())
							continue;

						UnaryOperationToken& token = tokens.createReplaceAt<UnaryOperationToken>(i);
						token.mOperator = op;
						token.mArgument = &leftToken.as<StatementToken>();

						tokens.erase(i-1);
						break;
					}

					default:
						break;
				}
			}
		}

		// Right to left associative: Go through in reverse order
		for (int i = (int)tokens.size() - 1; i >= 0; --i)
		{
			if (tokens[i].isA<OperatorToken>())
			{
				const Operator op = tokens[i].as<OperatorToken>().mOperator;
				switch (op)
				{
					case Operator::BINARY_MINUS:
					case Operator::UNARY_NOT:
					case Operator::UNARY_BITNOT:
					{
						CHECK_ERROR((size_t)(i+1) != tokens.size(), "Unary operator not allowed as last", mLineNumber);

						// Minus could be binary or unary... let's find out
						if (op == Operator::BINARY_MINUS && i > 0)
						{
							Token& leftToken = tokens[i-1];
							if (!leftToken.isA<OperatorToken>())
								continue;
						}

						Token& rightToken = tokens[i+1];
						CHECK_ERROR(rightToken.isStatement(), "Right of operator is no statement", mLineNumber);

						UnaryOperationToken& token = tokens.createReplaceAt<UnaryOperationToken>(i);
						token.mOperator = op;
						token.mArgument = &rightToken.as<StatementToken>();

						tokens.erase(i+1);
						break;
					}

					case Operator::UNARY_DECREMENT:
					case Operator::UNARY_INCREMENT:
					{
						// Prefix
						if ((size_t)(i+1) == tokens.size())
							continue;

						Token& rightToken = tokens[i+1];
						if (!rightToken.isStatement())
							continue;

						UnaryOperationToken& token = tokens.createReplaceAt<UnaryOperationToken>(i);
						token.mOperator = op;
						token.mArgument = &rightToken.as<StatementToken>();

						tokens.erase(i+1);
						break;
					}

					default:
						break;
				}
			}
		}
	}

	void TokenProcessing::processBinaryOperations(TokenList& tokens)
	{
		for (;;)
		{
			// Find operator with lowest priority
			uint8 bestPriority = 0xff;
			size_t bestPosition = 0;
			for (size_t i = 0; i < tokens.size(); ++i)
			{
				if (tokens[i].isA<OperatorToken>())
				{
					const Operator op = tokens[i].as<OperatorToken>().mOperator;
					CHECK_ERROR((i > 0 && i < tokens.size()-1) && (op != Operator::SEMICOLON_SEPARATOR), getOperatorNotAllowedErrorMessage(op), mLineNumber);

					const uint8 priority = OperatorHelper::getOperatorPriority(op);
					const bool isLower = (priority == bestPriority) ? OperatorHelper::isOperatorAssociative(op) : (priority < bestPriority);
					if (isLower)
					{
						bestPriority = priority;
						bestPosition = i;
					}
				}
			}

			if (bestPosition == 0)
				break;

			const Operator op = tokens[bestPosition].as<OperatorToken>().mOperator;
			Token& leftToken = tokens[bestPosition - 1];
			Token& rightToken = tokens[bestPosition + 1];

			CHECK_ERROR(leftToken.isStatement(), "Left of operator " << OperatorHelper::getOperatorCharacters(op) << " is no statement", mLineNumber);
			CHECK_ERROR(rightToken.isStatement(), "Right of operator " << OperatorHelper::getOperatorCharacters(op) << " is no statement", mLineNumber);

			BinaryOperationToken& token = tokens.createReplaceAt<BinaryOperationToken>(bestPosition);
			token.mOperator = op;
			token.mLeft = &leftToken.as<StatementToken>();
			token.mRight = &rightToken.as<StatementToken>();

			tokens.erase(bestPosition + 1);
			tokens.erase(bestPosition - 1);
		}
	}

	void TokenProcessing::evaluateCompileTimeConstants(TokenList& tokens)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			TokenPtr<StatementToken> resultTokenPtr;
			if (evaluateCompileTimeConstantsRecursive(tokens[i], resultTokenPtr))
			{
				tokens.replace(*resultTokenPtr, i);
			}
		}
	}

	bool TokenProcessing::evaluateCompileTimeConstantsRecursive(Token& inputToken, TokenPtr<StatementToken>& outTokenPtr)
	{
		switch (inputToken.getType())
		{
			case Token::Type::PARENTHESIS:
			{
				ParenthesisToken& pt = inputToken.as<ParenthesisToken>();
				if (pt.mParenthesisType == ParenthesisType::PARENTHESIS && pt.mContent.size() == 1 && pt.mContent[0].isStatement())
				{
					// Move parenthesis content to the output token pointer, so it will replace the parenthesis itself
					TokenPtr<StatementToken> tempContentPtr;
					tempContentPtr = pt.mContent[0].as<StatementToken>();	// This intermediate step is needed to avoid reference count reaching 0 in between
					outTokenPtr = tempContentPtr;
					pt.mContent.clear();
					return true;
				}
				break;
			}

			case Token::Type::UNARY_OPERATION:
			{
				UnaryOperationToken& uot = inputToken.as<UnaryOperationToken>();
				evaluateCompileTimeConstantsRecursive(*uot.mArgument, uot.mArgument);

				if (uot.mArgument->isA<ConstantToken>())
				{
					int64 resultValue;
					if (tryReplaceConstantsUnary(uot.mArgument->as<ConstantToken>(), uot.mOperator, resultValue))
					{
						const DataTypeDefinition* dataType = uot.mArgument->as<ConstantToken>().mDataType;	// Backup in case outTokenPtr is pointing to inputToken
						ConstantToken& token = outTokenPtr.create<ConstantToken>();
						token.mValue.set(resultValue);
						token.mDataType = dataType;
						return true;
					}
				}
				break;
			}

			case Token::Type::BINARY_OPERATION:
			{
				BinaryOperationToken& bot = inputToken.as<BinaryOperationToken>();
				evaluateCompileTimeConstantsRecursive(*bot.mLeft, bot.mLeft);
				evaluateCompileTimeConstantsRecursive(*bot.mRight, bot.mRight);

				if (bot.mLeft->isA<ConstantToken>() && bot.mRight->isA<ConstantToken>())
				{
					int64 resultValue;
					if (tryReplaceConstantsBinary(bot.mLeft->as<ConstantToken>(), bot.mRight->as<ConstantToken>(), bot.mOperator, resultValue))
					{
						const DataTypeDefinition* dataType = bot.mLeft->as<ConstantToken>().mDataType;	// Backup in case outTokenPtr is pointing to inputToken
						ConstantToken& token = outTokenPtr.create<ConstantToken>();
						token.mValue.set(resultValue);
						token.mDataType = dataType;
						return true;
					}
				}
				break;
			}

			case Token::Type::FUNCTION:
			{
				FunctionToken& ft = inputToken.as<FunctionToken>();
				bool allConstant = true;
				for (TokenPtr<StatementToken>& parameterTokenPtr : ft.mParameters)
				{
					evaluateCompileTimeConstantsRecursive(*parameterTokenPtr, parameterTokenPtr);
					if (!parameterTokenPtr->isA<ConstantToken>())
						allConstant = false;
				}

				if (allConstant)
				{
					// Compile-time evaluation of native functions that support it
					if (ft.mFunction->getType() == Function::Type::NATIVE && ft.mFunction->hasFlag(Function::Flag::COMPILE_TIME_CONSTANT))
					{
						RMX_CHECK(ft.mParameters.size() == ft.mFunction->getParameters().size(), "Different number of parameters", );
						Runtime emptyRuntime;
						ControlFlow controlFlow(emptyRuntime);
						for (size_t k = 0; k < ft.mParameters.size(); ++k)
						{
							ConstantToken& constantToken = *ft.mParameters[k].as<ConstantToken>();
							const Function::Parameter& parameter = ft.mFunction->getParameters()[k];
							castCompileTimeConstant(constantToken, parameter.mDataType);
							controlFlow.pushValueStack(constantToken.mValue);
						}
						static_cast<const NativeFunction*>(ft.mFunction)->mFunctionWrapper->execute(NativeFunction::Context(controlFlow));

						// Get return value from the stack and write it as constant
						const DataTypeDefinition* dataType = ft.mDataType;	// Backup in case outTokenPtr is pointing to inputToken
						const int64 resultValue = controlFlow.popValueStack<int64>();

						ConstantToken& token = outTokenPtr.create<ConstantToken>();
						token.mValue.set(resultValue);
						token.mDataType = dataType;
						return true;
					}
				}
				break;
			}

			default:
				break;
		}
		return false;
	}

	void TokenProcessing::resolveAddressOfFunctions(TokenList& tokens)
	{
		for (size_t i = 0; i + 1 < tokens.size(); ++i)
		{
			if (isKeyword(tokens[i], Keyword::ADDRESSOF))
			{
				CHECK_ERROR(isParenthesis(tokens[i+1], ParenthesisType::PARENTHESIS), "addressof must be followed by parentheses", mLineNumber);
				const TokenList& content = tokens[i+1].as<ParenthesisToken>().mContent;
				if (content.size() == 1 && content[0].isA<IdentifierToken>())
				{
					IdentifierToken& identifierToken = content[0].as<IdentifierToken>();
					const std::vector<Function*>& candidateFunctions = mGlobalsLookup.getFunctionsByName(identifierToken.mName.getHash());
					if (!candidateFunctions.empty())
					{
						uint32 address = 0;
						for (const Function* function : candidateFunctions)
						{
							if (function->getType() == Function::Type::SCRIPT)
							{
								const std::vector<uint32>& addressHooks = static_cast<const ScriptFunction*>(function)->getAddressHooks();
								if (!addressHooks.empty())
								{
									address = addressHooks[0];
									break;
								}
							}
						}
						CHECK_ERROR(address != 0, "No address hook found for function '" << identifierToken.mName.getString() << "'", mLineNumber);

						// Replace addressof and the parenthesis with the actual address as a constant
						ConstantToken& constantToken = tokens.createReplaceAt<ConstantToken>(i);
						constantToken.mValue.set(address);
						constantToken.mDataType = &PredefinedDataTypes::UINT_32;
						tokens.erase(i+1);
						break;
					}

					CHECK_ERROR(false, "Address of identifier '" << identifierToken.mName.getString() << "' could not be determined", mLineNumber);
				}
			}
		}
	}

	void TokenProcessing::resolveAddressOfMemoryAccesses(TokenList& tokens)
	{
		for (size_t i = 0; i + 1 < tokens.size(); ++i)
		{
			if (isKeyword(tokens[i], Keyword::ADDRESSOF))
			{
				CHECK_ERROR(isParenthesis(tokens[i+1], ParenthesisType::PARENTHESIS), "addressof must be followed by parentheses", mLineNumber);
				const TokenList& content = tokens[i+1].as<ParenthesisToken>().mContent;
				CHECK_ERROR(content.size() == 1, "Expected a single token in parentheses after addressof", mLineNumber);

				if (content[0].isA<MemoryAccessToken>())
				{
					// Replace addressof and the parenthesis with the actual address
					TokenPtr<StatementToken> addressToken = content[0].as<MemoryAccessToken>().mAddress;
					tokens.replace(*addressToken, i);
					tokens.erase(i+1);
				}
				else
				{
					// Assuming that all other possible use-cases for addressof were already processed before
					CHECK_ERROR(false, "Unsupported use of addressof", mLineNumber);
				}
			}
		}
	}

	TokenProcessing::BinaryOperationResult TokenProcessing::getBestOperatorSignature(Operator op, const DataTypeDefinition* leftDataType, const DataTypeDefinition* rightDataType)
	{
		BinaryOperationResult result;

		// Special handling for certain operations with strings
		if (mCompileOptions.mScriptFeatureLevel >= 2)
		{
			const BinaryOperationLookup* bestLookup = nullptr;
			uint16 bestPriority = 0xff00;
			for (const BinaryOperationLookup& lookup : mBinaryOperationLookup[(size_t)op])
			{
				// This is pretty much the same as "TypeCasting::getBestOperatorSignature", except for the "exactMatchLeftRequired" part
				const uint16 priority = mTypeCasting.getPriorityOfSignature(lookup.mSignature, leftDataType, rightDataType);
				if (priority < bestPriority)
				{
					bestLookup = &lookup;
					bestPriority = priority;
				}
			}

			if (nullptr != bestLookup)
			{
				result.mEnforcedFunction = (nullptr == bestLookup->mCachedBuiltinFunction || bestLookup->mCachedBuiltinFunction->mFunctions.empty()) ? nullptr : bestLookup->mCachedBuiltinFunction->mFunctions[0];
				result.mSignature = &bestLookup->mSignature;
				result.mSplitToOperator = bestLookup->mSplitToOperator;
				return result;
			}
		}

		// Choose best fitting signature
		{
			const std::vector<TypeCasting::BinaryOperatorSignature>& signatures = TypeCasting::getBinarySignaturesForOperator(op);
			const bool exactMatchLeftRequired = (OperatorHelper::getOperatorType(op) == OperatorHelper::OperatorType::ASSIGNMENT);
			const std::optional<size_t> bestIndex = mTypeCasting.getBestOperatorSignature(signatures, exactMatchLeftRequired, leftDataType, rightDataType);
			if (bestIndex.has_value())
			{
				result.mSignature = &signatures[*bestIndex];
				// If needed later, store the index in the token - this indirectly gives access to the chosen signature again
			}
			else
			{
				// Special handling for assignment of the same type
				if (leftDataType == rightDataType && op == Operator::ASSIGN)
				{
					static TypeCasting::BinaryOperatorSignature directSignature(leftDataType, rightDataType, leftDataType);
					result.mSignature = &directSignature;
				}
				else
				{
					CHECK_ERROR(false, "Cannot apply binary operator " << OperatorHelper::getOperatorCharacters(op) << " between types '" << leftDataType->getName() << "' and '" << rightDataType->getName() << "'", mLineNumber);
				}
			}
		}
		return result;
	}

	void TokenProcessing::assignStatementDataTypes(TokenList& tokens, const DataTypeDefinition* resultType)
	{
		for (size_t i = 0; i < tokens.size(); ++i)
		{
			if (tokens[i].isStatement())
			{
				assignStatementDataType(tokens[i].as<StatementToken>(), resultType);
			}
		}
	}

	const DataTypeDefinition* TokenProcessing::assignStatementDataType(StatementToken& token, const DataTypeDefinition* resultType)
	{
		switch (token.getType())
		{
			case Token::Type::CONSTANT:
			{
				if (token.mDataType->getClass() == DataTypeDefinition::Class::INTEGER)
				{
					if (nullptr != resultType && resultType->getClass() == DataTypeDefinition::Class::INTEGER)
					{
						// Let the constant use the result data type
						token.mDataType = resultType;
					}
					else
					{
						token.mDataType = &PredefinedDataTypes::CONST_INT;
					}
				}
				break;
			}

			case Token::Type::VARIABLE:
			{
				// Nothing to do, data type was already set when creating the token
				break;
			}

			case Token::Type::FUNCTION:
			{
				// Nothing to do, "processFunctionCalls" cared about everything already
				break;
			}

			case Token::Type::MEMORY_ACCESS:
			{
				// Nothing to do, "processMemoryAccesses" cared about everything already
				break;
			}

			case Token::Type::PARENTHESIS:
			{
				ParenthesisToken& pt = token.as<ParenthesisToken>();

				CHECK_ERROR(pt.mContent.size() == 1, "Parenthesis content must be one token", mLineNumber);
				CHECK_ERROR(pt.mContent[0].isStatement(), "Parenthesis content must be a statement", mLineNumber);

				StatementToken& innerStatement = pt.mContent[0].as<StatementToken>();
				token.mDataType = assignStatementDataType(innerStatement, resultType);
				break;
			}

			case Token::Type::UNARY_OPERATION:
			{
				UnaryOperationToken& uot = token.as<UnaryOperationToken>();
				token.mDataType = assignStatementDataType(*uot.mArgument, resultType);
				break;
			}

			case Token::Type::BINARY_OPERATION:
			{
				BinaryOperationToken& bot = token.as<BinaryOperationToken>();
				const OperatorHelper::OperatorType opType = OperatorHelper::getOperatorType(bot.mOperator);
				const DataTypeDefinition* expectedType = (opType == OperatorHelper::OperatorType::SYMMETRIC) ? resultType : nullptr;

				const DataTypeDefinition* leftDataType = assignStatementDataType(*bot.mLeft, expectedType);
				const DataTypeDefinition* rightDataType = assignStatementDataType(*bot.mRight, (opType == OperatorHelper::OperatorType::ASSIGNMENT) ? leftDataType : expectedType);

				const BinaryOperationResult result = getBestOperatorSignature(bot.mOperator, leftDataType, rightDataType);
				if (nullptr == result.mEnforcedFunction)
				{
					// Default behavior: Use the found signature
					bot.mDataType = result.mSignature->mResult;

					if (opType != OperatorHelper::OperatorType::TRINARY)
					{
						// Add implicit casts where needed
						insertCastTokenIfNecessary(bot.mLeft, result.mSignature->mLeft);
						insertCastTokenIfNecessary(bot.mRight, result.mSignature->mRight);
					}
				}
				else
				{
					if (result.mSplitToOperator == Operator::_INVALID)
					{
						// Use the enforced function
						bot.mFunction = result.mEnforcedFunction;
						bot.mDataType = result.mSignature->mResult;
					}
					else
					{
						// Split an operator like "A += B" into "A = A + B"
						TokenPtr<StatementToken> newRightSide;
						BinaryOperationToken& newRightBot = newRightSide.create<BinaryOperationToken>();
						newRightBot.mOperator = result.mSplitToOperator;
						newRightBot.mLeft = bot.mLeft;
						newRightBot.mRight = bot.mRight;
						newRightBot.mFunction = result.mEnforcedFunction;
						newRightBot.mDataType = result.mSignature->mResult;
						bot.mOperator = Operator::ASSIGN;
						bot.mRight = newRightSide;
						bot.mDataType = result.mSignature->mResult;
					}
				}
				break;
			}

			case Token::Type::VALUE_CAST:
			{
				ValueCastToken& vct = token.as<ValueCastToken>();

				// This token has the correct data type assigned already
				//  -> What's left is determining its contents' data type
				assignStatementDataType(*vct.mArgument, token.mDataType);

				// Check if types fit together at all
				const DataTypeDefinition& original = *vct.mArgument->mDataType;
				const DataTypeDefinition& target = *vct.mDataType;
				CHECK_ERROR(mTypeCasting.canExplicitlyCastTypes(original, target), "Explicit cast not possible from " << original.getName().getString() << " to " << target.getName().getString(), mLineNumber);
				break;
			}

			default:
				break;
		}
		return token.mDataType;
	}

	const Variable* TokenProcessing::findVariable(uint64 nameHash)
	{
		// Search for local variables first
		const Variable* variable = findLocalVariable(nameHash);
		if (nullptr != variable)
			return variable;

		// Maybe it's a global variable
		const GlobalsLookup::Identifier* resolvedIdentifier = mGlobalsLookup.resolveIdentifierByHash(nameHash);
		if (nullptr != resolvedIdentifier && resolvedIdentifier->getType() == GlobalsLookup::Identifier::Type::VARIABLE)
		{
			return &resolvedIdentifier->as<Variable>();
		}

		// Not found
		return nullptr;
	}

	LocalVariable* TokenProcessing::findLocalVariable(uint64 nameHash)
	{
		return findInList(*mContext.mLocalVariables, nameHash);
	}

	const ConstantArray* TokenProcessing::findConstantArray(uint64 nameHash)
	{
		// Search for local constant arrays first
		const ConstantArray* constantArray = findInList(*mContext.mLocalConstantArrays, nameHash);
		if (nullptr == constantArray)
		{
			// Maybe it's a global constant array
			const GlobalsLookup::Identifier* resolvedIdentifier = mGlobalsLookup.resolveIdentifierByHash(nameHash);
			if (nullptr != resolvedIdentifier && resolvedIdentifier->getType() == GlobalsLookup::Identifier::Type::CONSTANT_ARRAY)
			{
				constantArray = &resolvedIdentifier->as<ConstantArray>();
			}
		}
		return constantArray;
	}

}
