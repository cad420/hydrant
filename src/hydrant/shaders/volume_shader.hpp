#include <glm_math.hpp>
#include <texture_adapter.hpp>
#include "../shader.hpp"

VM_BEGIN_MODULE( hydrant )

VM_EXPORT
{
	struct StandardVolumePixel
	{
		void write_to( unsigned char dst[ 4 ] )
		{
			auto v = glm::clamp( this->v * 255.f, vec4( 0.f ), vec4( 255.f ) );
			dst[ 0 ] = (unsigned char)( v.x );
			dst[ 1 ] = (unsigned char)( v.y );
			dst[ 2 ] = (unsigned char)( v.z );
			dst[ 3 ] = (unsigned char)( 255 );
		}

	public:
		// struct Record
		// {
		// 	vec4 v, u;
		// 	vec3 p;
		// 	int n;
		// };

		// Record rem[ MAX_REMAINING ];
		vec4 v;
		Ray ray;
		int nsteps;
	};

	struct Integrator
	{
		using Pixel = StandardVolumePixel;

		__device__ bool
		  integrate( glm::vec3 const &p, glm::vec3 const &ip, StandardVolumePixel &pixel ) const
		{
			const auto opacity_threshold = 0.95f;
			const auto density = 3e-4f;

			auto val = glm::vec4( thumbnail_tex.sample_3d<float2>( ip ).x );
			auto col = glm::vec4( ip, 1 ) * val * density;
			pixel.v += col * ( 1.f - pixel.v.w );

			return pixel.v.y <= opacity_threshold;
		}

		__device__ float
		  chebyshev( glm::vec3 const &ip ) const
		{
			return thumbnail_tex.sample_3d<float2>( ip ).y;
		}

	public:
		TextureAdapter thumbnail_tex;
	};

	struct VolumnRayEmitShader : RayEmitShader, Integrator
	{
		using Pixel = StandardVolumePixel;

		__device__ int
		  skip_nblock_steps( Ray &ray, glm::vec3 const &ip,
							 float nblocks, float cdu, float step ) const
		{
			float tnear, tfar;
			ray.intersect( Box3D{ ip, ip + 1.f }, tnear, tfar );
			float di = ceil( ( tfar + ( nblocks - 1 ) * cdu ) / step );
			ray.o += ray.d * di * step;
			return (int)di;
		}

		__device__ void
		  apply( Ray const &ray_in, Pixel &pixel_out ) const
		{
#define MAXX ( 8 )
			__shared__ uvec3 absent_coord[ MAXX ][ MAXX ][ MAXX ];
			__shared__ bool is_absent[ MAXX ][ MAXX ][ MAXX ];
			__shared__ int wg_emit_cnt;

			bool is_worker = threadIdx.x < MAXX && threadIdx.y < MAXX;
			if ( is_worker ) {
				for ( int i = 0; i != MAXX; ++i ) {
					is_absent[ threadIdx.y ][ threadIdx.x ][ i ] = false;
				}
			}
			wg_emit_cnt = 0;

			__syncthreads();

			const auto step = 1e-2f * th_4;
			const auto cdu = 1.f / glm::compMax( glm::abs( ray_in.d ) );

			auto ray = ray_in;
			auto pixel = pixel_out;
			float tnear, tfar;
			if ( ray.intersect( bbox, tnear, tfar ) ) {
				ray.o += ray.d * tnear;
				auto nsteps = min( max_steps, int( ( tfar - tnear ) / step ) );

				int i;
				for ( i = 0; i < nsteps; ++i ) {
					vec3 ip = floor( ray.o );
					if ( float cd = this->chebyshev( ip ) ) {
						i += skip_nblock_steps( ray, ip, cd, cdu, step );
					} else {
						ivec3 ipm = mod( ip, vec3( MAXX ) );
						absent_coord[ ipm.x ][ ipm.y ][ ipm.z ] = ip;
						is_absent[ ipm.x ][ ipm.y ][ ipm.z ] = true;
						if ( !this->integrate( ray.o, ip, pixel ) ) {
							break;
						}
					}
					ray.o += ray.d * step;
				}

				pixel.nsteps = nsteps - i;
				pixel.ray = ray;
			} else {
				pixel.nsteps = 0;
			}
			pixel_out = pixel;

			__syncthreads();

			int wg_id = blockIdx.x + blockIdx.y * blockDim.x;
			char *wg_base_ptr = absent_buf.ptr() + wg_len_bytes * wg_id;
			int *wg_emit_cnt_ptr = (int *)wg_base_ptr;
			uvec3 *wg_ptr = (uvec3 *)( wg_base_ptr + sizeof( int ) );

			if ( is_worker ) {
				for ( int i = 0; i != MAXX; ++i ) {
					if ( is_absent[ threadIdx.y ][ threadIdx.x ][ i ] ) {
						auto old = atomicAdd( &wg_emit_cnt, 1 );
						if ( old < wg_max_emit_cnt ) {
							wg_ptr[ old ] = absent_coord[ threadIdx.y ][ threadIdx.x ][ i ];
						}
					}
				}
			}

			__syncthreads();

			if ( threadIdx.x == 0 && threadIdx.y == 0 ) {
				*wg_emit_cnt_ptr = min( wg_emit_cnt, wg_max_emit_cnt );
			}
		}

	public:
		Box3D bbox;
		float th_4;
		int max_steps = 500;

		cufx::MemoryView1D<char> absent_buf;
		int wg_max_emit_cnt;
		int wg_len_bytes;
	};

	// struct VolumePixelShader : PixelShader
	// {
	// 	__device__ void
	// 	  apply( Pixel &pixel_in_out ) const
	// 	{
	// 		// pixel_in_out.
	// 		// for ( int i = 0; i < nsteps; ++i ) {
	// 		// 	ray.o += ray.d * step;
	// 		// 	glm::vec3 ip = floor( ray.o );
	// 		// 	if ( float cd = this->chebyshev( ip ) ) {
	// 		// 		i += skip_nblock_steps( ray, ip, cd, cdu, step );
	// 		// 	} else if ( !this->integrate( ray.o, ip, pixel ) ) {
	// 		// 		break;
	// 		// 	}
	// 		// }
	// 	}
	// };
}

VM_END_MODULE()
