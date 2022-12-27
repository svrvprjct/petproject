#pragma once

#include <Windows.h>
#include <DirectXMath.h>
#include <cstdint>

class ENGINE_API MathHelper
{
public:
	// Returns random float in [0, 1).
	static float RandF();
	// Returns random float in [a, b).
	static float RandF(float a, float b);
	static int Rand(int a, int b);
	template<typename T>
	static T Min(const T& a, const T& b);
	template<typename T>
	static T Max(const T& a, const T& b);
	template<typename T>
	static T Lerp(const T& a, const T& b, float t);
	template<typename T>
	static T Clamp(const T& x, const T& low, const T& high);
	// Returns the polar angle of the point (x,y) in [0, 2*PI).
	static float AngleFromXY(float x, float y);
	static DirectX::XMVECTOR SphericalToCartesian(float radius, float theta, float phi);
	static DirectX::XMMATRIX InverseTranspose(DirectX::CXMMATRIX M);
	static DirectX::XMFLOAT4X4 Identity4x4();
	static DirectX::XMVECTOR RandUnitVec3();
	static DirectX::XMVECTOR RandHemisphereUnitVec3(DirectX::XMVECTOR n);

	static const float Infinity;
	static const float Pi;
};
