/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2022 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once

#include "lemon/utility/FlyweightString.h"


namespace lemon
{

	enum class BaseType : uint8
	{
		VOID	  = 0x00,
		UINT_8	  = 0x10 + 0x00,
		UINT_16	  = 0x10 + 0x01,
		UINT_32	  = 0x10 + 0x02,
		UINT_64	  = 0x10 + 0x03,
		INT_8	  = 0x18 + 0x00,
		INT_16	  = 0x18 + 0x01,
		INT_32	  = 0x18 + 0x02,
		INT_64	  = 0x18 + 0x03,
		BOOL	  = UINT_8,
		INT_CONST = 0x1f			// Constants have an undefined int type
	};

	enum class BaseCastType : uint8
	{
		INVALID = 0xff,
		NONE    = 0x00,

		// Cast up (value is unsigned -> adding zeroes)
		UINT_8_TO_16  = 0x01,	// 0x00 * 4 + 0x01
		UINT_8_TO_32  = 0x02,	// 0x00 * 4 + 0x02
		UINT_8_TO_64  = 0x03,	// 0x00 * 4 + 0x03
		UINT_16_TO_32 = 0x06,	// 0x01 * 4 + 0x02
		UINT_16_TO_64 = 0x07,	// 0x01 * 4 + 0x03
		UINT_32_TO_64 = 0x0b,	// 0x02 * 4 + 0x03

		// Cast down (signed or unsigned makes no difference here)
		INT_16_TO_8  = 0x04,	// 0x01 * 4 + 0x00
		INT_32_TO_8  = 0x08,	// 0x02 * 4 + 0x00
		INT_64_TO_8  = 0x0c,	// 0x03 * 4 + 0x00
		INT_32_TO_16 = 0x09,	// 0x02 * 4 + 0x01
		INT_64_TO_16 = 0x0d,	// 0x03 * 4 + 0x01
		INT_64_TO_32 = 0x0e,	// 0x03 * 4 + 0x02

		// Cast up (value is signed -> adding highest bit)
		SINT_8_TO_16  = 0x11,	// 0x10 + 0x00 * 4 + 0x01
		SINT_8_TO_32  = 0x12,	// 0x10 + 0x00 * 4 + 0x02
		SINT_8_TO_64  = 0x13,	// 0x10 + 0x00 * 4 + 0x03
		SINT_16_TO_32 = 0x16,	// 0x10 + 0x01 * 4 + 0x02
		SINT_16_TO_64 = 0x17,	// 0x10 + 0x01 * 4 + 0x03
		SINT_32_TO_64 = 0x1b	// 0x10 + 0x02 * 4 + 0x03
	};



	struct DataTypeDefinition
	{
	public:
		enum class Class : uint8
		{
			VOID,
			INTEGER,
			STRING
		};

	public:
		inline DataTypeDefinition(const char* name, Class class_, size_t bytes, BaseType baseType) :
			mNameString(name),
			mClass(class_),
			mBytes(bytes),
			mBaseType(baseType)
		{}
		virtual ~DataTypeDefinition() {}

		template<typename T> const T& as() const  { return static_cast<const T&>(*this); }

		FlyweightString getName() const;
		inline size_t getBytes() const		{ return mBytes; }
		inline Class getClass() const		{ return mClass; }
		inline BaseType getBaseType() const	{ return mBaseType; }

		virtual uint32 getDataTypeHash() const = 0;

	private:
		const char* mNameString;
		mutable FlyweightString mName;

		const size_t mBytes = 0;
		const Class mClass = Class::VOID;
		const BaseType mBaseType = BaseType::VOID;	// If compatible to a base type (from the runtime's point of view), set this to something different than VOID
	};


	struct VoidDataType : public DataTypeDefinition
	{
	public:
		inline VoidDataType() :
			DataTypeDefinition("void", Class::VOID, 0, BaseType::VOID)
		{}

		uint32 getDataTypeHash() const override  { return 0; }
	};


	struct IntegerDataType : public DataTypeDefinition
	{
	public:
		enum class Semantics
		{
			DEFAULT,
			CONSTANT,
			BOOLEAN
		};

		const Semantics mSemantics = Semantics::DEFAULT;
		const bool mIsSigned = false;
		const uint8 mSizeBits = 0;	// 0 for 8-bit data types, 1 for 16-bit, 2 for 32-bit, 3 for 64-bit

	public:
		inline IntegerDataType(const char* name, size_t bytes, Semantics semantics, bool isSigned, BaseType baseType) :
			DataTypeDefinition(name, Class::INTEGER, bytes, baseType),
			mSemantics(semantics),
			mSizeBits((bytes == 1) ? 0 : (bytes == 2) ? 1 : (bytes == 4) ? 2 : 3),
			mIsSigned(isSigned)
		{}

		uint32 getDataTypeHash() const override;
	};


	struct StringDataType : public DataTypeDefinition
	{
	public:
		inline StringDataType() :
			DataTypeDefinition("string", Class::STRING, 8, BaseType::UINT_64)
		{}

		// Rather unfortunately, the data type hash for string needs to be the same as for u64, for feature level 1 compatibility regarding function overloading
		uint32 getDataTypeHash() const override  { return 0x01000008; }
	};


	struct PredefinedDataTypes
	{
		inline static const VoidDataType VOID		  = VoidDataType();

		inline static const IntegerDataType UINT_8	  = IntegerDataType("u8",  1, IntegerDataType::Semantics::DEFAULT, false, BaseType::UINT_8);
		inline static const IntegerDataType UINT_16	  = IntegerDataType("u16", 2, IntegerDataType::Semantics::DEFAULT, false, BaseType::UINT_16);
		inline static const IntegerDataType UINT_32	  = IntegerDataType("u32", 4, IntegerDataType::Semantics::DEFAULT, false, BaseType::UINT_32);
		inline static const IntegerDataType UINT_64	  = IntegerDataType("u64", 8, IntegerDataType::Semantics::DEFAULT, false, BaseType::UINT_64);
		inline static const IntegerDataType INT_8	  = IntegerDataType("s8",  1, IntegerDataType::Semantics::DEFAULT, true,  BaseType::INT_8);
		inline static const IntegerDataType INT_16	  = IntegerDataType("s16", 2, IntegerDataType::Semantics::DEFAULT, true,  BaseType::INT_16);
		inline static const IntegerDataType INT_32	  = IntegerDataType("s32", 4, IntegerDataType::Semantics::DEFAULT, true,  BaseType::INT_32);
		inline static const IntegerDataType INT_64	  = IntegerDataType("s64", 8, IntegerDataType::Semantics::DEFAULT, true,  BaseType::INT_64);
		inline static const IntegerDataType CONST_INT = IntegerDataType("const_int", 8, IntegerDataType::Semantics::CONSTANT, true, BaseType::INT_CONST);
		//inline static const IntegerDataType BOOL	  = IntegerDataType("bool", 1, IntegerDataType::Semantics::BOOLEAN, false, BaseType::UINT_8);
		inline static const IntegerDataType& BOOL	  = UINT_8;

		inline static const StringDataType STRING     = StringDataType();
	};


	struct DataTypeHelper
	{
		static size_t getSizeOfBaseType(BaseType baseType);
		static const DataTypeDefinition* getDataTypeDefinitionForBaseType(BaseType baseType);
	};


	struct DataTypeSerializer
	{
		static const DataTypeDefinition* readDataType(VectorBinarySerializer& serializer);
		static void writeDataType(VectorBinarySerializer& serializer, const DataTypeDefinition* const dataTypeDefinition);
		static void serializeDataType(VectorBinarySerializer& serializer, const DataTypeDefinition*& dataTypeDefinition);
	};

}
