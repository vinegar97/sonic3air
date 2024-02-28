/*
*	rmx Library
*	Copyright (C) 2008-2024 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#pragma once


// Definitions
#define PI_FLOAT  3.14159265f
#define PI_DOUBLE 3.1415926535897932


float rad2deg(float radians);
float deg2rad(float degrees);


#include "rmxbase/math/Vec2.h"
#include "rmxbase/math/Vec3.h"
#include "rmxbase/math/Vec4.h"
#include "rmxbase/math/Mat3.h"
#include "rmxbase/math/Mat4.h"
#include "rmxbase/math/Rect.h"
#include "rmxbase/math/Box2.h"
#include "rmxbase/math/Box3.h"
#include "rmxbase/math/Line.h"
#include "rmxbase/math/Ray.h"
#include "rmxbase/math/Plane.h"


inline float rad2deg(float radians)  { return radians * (180.0f / PI_FLOAT); }
inline float deg2rad(float degrees)  { return degrees * (PI_FLOAT / 180.0f); }


namespace math
{
	FUNCTION_EXPORT bool intersectRayWithPlane(const Ray& ray, const Plane& plane, Vec3f* outIntersectionPoint = nullptr, float* outIntersectionT = nullptr);
	FUNCTION_EXPORT bool intersectLineWithPlane(const Line& line, const Plane& plane, Vec3f* outIntersectionPoint = nullptr, float* outIntersectionT = nullptr);
	FUNCTION_EXPORT bool nearestPointBetweenLines(const Line& line1, const Line& line2, Vec3f* outPoint = nullptr, float* outT = nullptr);
	FUNCTION_EXPORT Vec3f nearestPointOnLine(const Vec3f& point, const Line& line);
}
