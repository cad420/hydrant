#pragma once

#include <cuda_runtime.h>
#include <glmcuda/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glmcuda/gtx/io.hpp>
#include <glmcuda/gtx/component_wise.hpp>
#include <glmcuda/gtc/matrix_transform.hpp>
#include <VMUtils/fmt.hpp>
#include <VMUtils/attributes.hpp>
#include <VMUtils/modules.hpp>
#include <VMUtils/json_binding.hpp>

namespace glm
{
template <length_t N, typename T, qualifier Q>
void to_json( nlohmann::json &j, const vec<N, T, Q> &v )
{
	j = nlohmann::json::array();
	for ( auto i = 0; i < N; ++i ) {
		j[ i ] = v[ i ];
	}
}

template <length_t N, typename T, qualifier Q>
inline void from_json( const nlohmann::json &j, vec<N, T, Q> &v )
{
	for ( auto i = 0; i < N; ++i ) {
		v[ i ] = j[ i ].get<T>();
	}
}
}  // namespace glm

VM_BEGIN_MODULE( hydrant )

using namespace glm;

VM_EXPORT
{
	struct Box3D
	{
		VM_DEFINE_ATTRIBUTE( vec3, min );
		VM_DEFINE_ATTRIBUTE( vec3, max );

		__host__ __device__ bool
		  contains( vec3 const &pt ) const
		{
			return glm::all( glm::greaterThanEqual( pt, min ) ) &&
				   glm::all( glm::lessThanEqual( pt, max ) );
		}
	};

	struct Ray
	{
		VM_DEFINE_ATTRIBUTE( vec3, o );
		VM_DEFINE_ATTRIBUTE( vec3, d );

		__host__ __device__ bool
		  intersect( Box3D const &box, float &tnear, float &tfar ) const
		{
			vec3 invr = vec3{ 1., 1., 1. } / d;
			vec3 tbot = invr * ( box.min - o );
			vec3 ttop = invr * ( box.max - o );

			vec3 tmin = min( ttop, tbot );
			vec3 tmax = max( ttop, tbot );

			tnear = glm::compMax( tmin );
			tfar = glm::compMin( tmax );

			return tfar > tnear;
		}
	};

	template <length_t N, typename T,
			  typename = typename std::enable_if<std::is_floating_point<T>::value>::type>
	inline vec<N, unsigned char> saturate( vec<N, T> const &fp )
	{
		return vec<N, unsigned char>( clamp( fp * 255.f, vec<N, T>( 0. ), vec<N, T>( 255. ) ) );
	}

	template <length_t N, typename T,
			  typename = typename std::enable_if<std::is_integral<T>::value>::type>
	inline vec<N, float> saturate( vec<N, T> const &ip )
	{
		return clamp( vec<N, float>( ip ) / 255.f, vec<N, float>( 0. ), vec<N, float>( 1. ) );
	}
}

template <length_t N, typename T, bool = std::is_integral<T>::value>
struct SaturateToFloat;

template <length_t N, typename T>
struct SaturateToFloat<N, T, true>
{
	static vec<N, float> apply( vec<N, T> const &ip ) { return saturate( ip ); }
};

template <length_t N, typename T>
struct SaturateToFloat<N, T, false>
{
	static vec<N, float> apply( vec<N, T> const &fp )
	{
		return clamp( vec<N, float>( fp ), vec<N, float>( 0. ), vec<N, float>( 1. ) );
	}
};

VM_EXPORT
{
	template <length_t N, typename T>
	inline vec<N, float> saturate_to_float( vec<N, T> const &p )
	{
		return SaturateToFloat<N, T>::apply( p );
	}
}

VM_END_MODULE()
