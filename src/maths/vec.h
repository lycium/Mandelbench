#pragma once

#include <cmath>

#include "real.h"



template<int n, typename real_type>
struct vec
{
	real_type e[n];


	constexpr inline vec() { }
	constexpr inline vec(const vec & v) { for (int i = 0; i < n; ++i) e[i] = v.e[i]; }
	constexpr inline vec(const real_type & v) { for (int i = 0; i < n; ++i) e[i] = v; }

	// Some brutal C++ hackery to enable initializer lists
	template<typename val, typename... vals, std::enable_if_t<(sizeof...(vals) > 0), int> = 0>
	constexpr inline vec(const val v, const vals... vs) : e { (real_type)v, (real_type)vs... } { }

	constexpr inline vec operator+(const vec & rhs) const { vec r; for (int i = 0; i < n; ++i) r.e[i] = e[i] + rhs.e[i]; return r; }
	constexpr inline vec operator-(const vec & rhs) const { vec r; for (int i = 0; i < n; ++i) r.e[i] = e[i] - rhs.e[i]; return r; }
	constexpr inline vec operator*(const real_type & rhs) const { vec r; for (int i = 0; i < n; ++i) r.e[i] = e[i] * rhs; return r; }
	constexpr inline vec operator/(const real_type & rhs) const { return *this * (real_type(1) / rhs); }

	constexpr inline vec operator*(const vec & rhs) const { vec r; for (int i = 0; i < n; ++i) r.e[i] = e[i] * rhs.e[i]; return r; }
	constexpr inline vec operator/(const vec & rhs) const { vec r; for (int i = 0; i < n; ++i) r.e[i] = e[i] / rhs.e[i]; return r; }

	constexpr inline vec operator-() const { vec r; for (int i = 0; i < n; ++i) r.e[i] = -e[i]; return r; }

	constexpr inline const vec & operator =(const vec & rhs) { for (int i = 0; i < n; ++i) e[i]  = rhs.e[i]; return *this; }
	constexpr inline const vec & operator+=(const vec & rhs) { for (int i = 0; i < n; ++i) e[i] += rhs.e[i]; return *this; }
	constexpr inline const vec & operator-=(const vec & rhs) { for (int i = 0; i < n; ++i) e[i] -= rhs.e[i]; return *this; }
	constexpr inline const vec & operator*=(const vec & rhs) { for (int i = 0; i < n; ++i) e[i] *= rhs.e[i]; return *this; }
	constexpr inline const vec & operator/=(const vec & rhs) { for (int i = 0; i < n; ++i) e[i] /= rhs.e[i]; return *this; }

	constexpr inline const vec & operator =(const real_type & rhs) { for (int i = 0; i < n; ++i) e[i]  = rhs; return *this; }
	constexpr inline const vec & operator+=(const real_type & rhs) { for (int i = 0; i < n; ++i) e[i] += rhs; return *this; }
	constexpr inline const vec & operator-=(const real_type & rhs) { for (int i = 0; i < n; ++i) e[i] -= rhs; return *this; }
	constexpr inline const vec & operator*=(const real_type & rhs) { for (int i = 0; i < n; ++i) e[i] *= rhs; return *this; }
	constexpr inline const vec & operator/=(const real_type & rhs)
	{
		const real_type s = real_type(1) / rhs;
		for (int i = 0; i < n; ++i)
			e[i] *= s;
		return *this;
	}

	constexpr inline real_type & x() { return e[0]; }
	constexpr inline real_type & y() { return e[1]; }
	constexpr inline real_type & z() { return e[2]; }
	constexpr inline real_type & w() { return e[3]; }
	constexpr inline const real_type & x() const { return e[0]; }
	constexpr inline const real_type & y() const { return e[1]; }
	constexpr inline const real_type & z() const { return e[2]; }
	constexpr inline const real_type & w() const { return e[3]; }
};


template<int n, typename real_type>
inline real_type dot(const vec<n, real_type> & lhs, const vec<n, real_type> & rhs)
{
	real_type d = 0;
	for (int i = 0; i < n; ++i)
		d += lhs.e[i] * rhs.e[i];
	return d;
}


template<int n, typename real_type>
inline real_type length2(const vec<n, real_type> & v)
{
	real_type d = 0;
	for (int i = 0; i < n; ++i)
		d += v.e[i] * v.e[i];
	return d;
}


template<int n, typename real_type>
inline real_type length(const vec<n, real_type> & v) { return std::sqrt(length2(v)); }


template<int n, typename real_type>
inline vec<n, real_type> normalise(const vec<n, real_type> & v, const real_type len = 1) { return v * (len / length(v)); }


template<typename real_type>
inline vec<3, real_type> cross(const vec<3, real_type> & a, const vec<3, real_type> & b)
{
	return vec<3, real_type>(
		a.y() * b.z() - a.z() * b.y(),
		a.z() * b.x() - a.x() * b.z(),
		a.x() * b.y() - a.y() * b.x());
}

template<int n, typename real_type>
constexpr inline vec<n, real_type> max(const real_type lhs, const vec<n, real_type> & rhs) { vec<n, real_type> r; for (int i = 0; i < n; ++i) r.e[i] = std::max(lhs, rhs.e[i]); return r; }

using vec2i = vec<2, int>;
using vec2r = vec<2, real>;
using vec2f = vec<2, float>;
using vec2d = vec<2, double>;


using vec3i = vec<3, int>;
using vec3r = vec<3, real>;
using vec3f = vec<3, float>;
using vec3d = vec<3, double>;
