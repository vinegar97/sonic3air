/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2025 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/helper/JsonSerializer.h"
#include "oxygen/helper/JsonHelper.h"


bool JsonSerializer::serialize(const char* key, bool& value)
{
	if (mReading)
	{
		return JsonHelper(*mCurrentJson).tryReadBool(key, value);
	}
	else
	{
		(*mCurrentJson)[key] = value;
		return true;
	}
}

bool JsonSerializer::serialize(const char* key, int& value)
{
	if (mReading)
	{
		return JsonHelper(*mCurrentJson).tryReadInt(key, value);
	}
	else
	{
		(*mCurrentJson)[key] = value;
		return true;
	}
}

bool JsonSerializer::serialize(const char* key, float& value)
{
	if (mReading)
	{
		return JsonHelper(*mCurrentJson).tryReadFloat(key, value);
	}
	else
	{
		(*mCurrentJson)[key] = value;
		return true;
	}
}

bool JsonSerializer::serialize(const char* key, std::string& value)
{
	if (mReading)
	{
		return JsonHelper(*mCurrentJson).tryReadString(key, value);
	}
	else
	{
		(*mCurrentJson)[key] = value;
		return true;
	}
}

bool JsonSerializer::serialize(const char* key, std::wstring& value)
{
	if (mReading)
	{
		return JsonHelper(*mCurrentJson).tryReadString(key, value);
	}
	else
	{
		(*mCurrentJson)[key] = WString(value).toUTF8().toStdString();
		return true;
	}
}

bool JsonSerializer::serializeComponents(const char* key, Vec2i& value)
{
	bool result = true;
	result &= serialize((key + std::string("X")).c_str(), value.x);
	result &= serialize((key + std::string("Y")).c_str(), value.y);
	return result;
}

bool JsonSerializer::serializeVectorAsSizeString(const char* key, Vec2i& value)
{
	if (mReading)
	{
		std::string str;
		serialize(key, str);
		std::vector<String> components;
		String(str).split(components, 'x');
		if (components.size() < 2)
			return false;

		value.x = components[0].parseInt();
		value.y = components[1].parseInt();
		return true;
	}
	else
	{
		std::string str = *String(0, "%d x %d", value.x, value.y);
		return serialize(key, str);
	}
}

bool JsonSerializer::serializeHexColorRGB(const char* key, Color& value)
{
	std::string str;
	if (mReading)
	{
		if (!serialize(key, str))
			return false;

		value.setARGB32((uint32)rmx::parseInteger(str) | 0xff000000);
		return true;
	}
	else
	{
		str = rmx::hexString(value.getARGB32() & 0xffffff, 6);
		return serialize(key, str);
	}
}

bool JsonSerializer::serializeArray(const char* key, std::vector<std::string>& value)
{
	if (mReading)
	{
		value.clear();
		const Json::Value& jsonArray = (*mCurrentJson)[key];
		if (!jsonArray.isArray())
			return false;

		for (Json::Value::ArrayIndex k = 0; k < jsonArray.size(); ++k)
		{
			if (!jsonArray[k].isString())
				return false;
			value.emplace_back(jsonArray[k].asString());
		}
	}
	else
	{
		Json::Value jsonArray(Json::arrayValue);
		for (const std::string& str : value)
			jsonArray.append(str);
		(*mCurrentJson)[key] = jsonArray;
	}
	return true;
}

bool JsonSerializer::beginObject(const char* key)
{
	if (mReading && !(*mCurrentJson)[key].isObject())
		return false;

	mObjectStack.push_back(mCurrentJson);
	mCurrentJson = &(*mCurrentJson)[key];
	return true;
}

void JsonSerializer::endObject()
{
	if (mObjectStack.empty())
	{
		RMX_ASSERT(false, "Ending JSON object without a corresponing begin");
		return;
	}

	mCurrentJson = mObjectStack.back();
	mObjectStack.pop_back();
}
