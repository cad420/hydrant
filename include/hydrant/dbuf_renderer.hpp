#pragma once

#include <varch/thumbnail.hpp>
#include <hydrant/dyn_kd_tree.hpp>
#include <hydrant/basic_renderer.hpp>
#include <hydrant/double_buffering.hpp>
#include <hydrant/octree_culler.hpp>

VM_BEGIN_MODULE( hydrant )

VM_EXPORT
{
	struct DbufRtRenderCtx : vm::Dynamic
	{
	};
	
	template <typename Shader>
	struct DbufRenderer : BasicRenderer<Shader>
	{
		void realtime_render_dynamic( IRenderLoop &loop, MpiComm const &comm )
		{
			std::unique_ptr<DbufRtRenderCtx> ctx( create_dbuf_rt_render_ctx() );
			OctreeCuller culler( this->exhibit, this->chebyshev_thumb );

			std::vector<std::size_t> render_t( comm.size );
			std::vector<std::pair<float, int>> dist( comm.size );
			std::vector<int> z_order( comm.size );
			DynKdTree kd_tree( this->dim, comm.size );
				
			FnDoubleBuffering loop_drv(
			  ImageOptions{}
			    .set_device( this->device )
			    .set_resolution( this->resolution ),
			  loop,
			  [&]( auto &frame, auto frame_idx ) {
				  auto bbox = kd_tree.search( comm.rank );
				  culler.set_bbox( bbox );
				  this->shader.bbox = Box3D{ bbox.min, bbox.max };

				  auto orig = culler.get_orig( loop.camera );
				  dist[ comm.rank ].first = dist_point_bbox( orig, this->shader.bbox );
				  for ( int i = 0; i < comm.size; ++i ) {
					  MPI_Bcast( &dist[ i ].first, sizeof( float ), MPI_CHAR, i, comm.comm );
					  dist[ i ].second = i;
				  }
				  MPI_Barrier( comm.comm );
				  std::sort( dist.begin(), dist.end(),
							 []( auto &x, auto &y ) { return x.first < y.first; } );
				  std::transform( dist.begin(), dist.end(), z_order.begin(),
								  []( auto &x ) { return x.second; } );

				  render_t[ comm.rank ] = dbuf_rt_render_frame( frame, *ctx,
																loop, culler, comm,
																z_order );
				  for ( int i = 0; i < comm.size; ++i ) {
					  MPI_Bcast( &render_t[ i ], sizeof( std::size_t ), MPI_CHAR, i, comm.comm );
				  }
				  MPI_Barrier( comm.comm );
				  
				  kd_tree.update( render_t );
			  },
			  [&]( auto &frame, auto frame_idx ) {
				  auto fp = frame.fetch_data();
				  loop.on_frame( fp );
			  });

			loop_drv.run();
		}

	private:
		float dist_point_bbox( vec3 const &pt, Box3D const &bbox )
		{
			auto a = bbox.min + .5f;
			auto b = bbox.max - .5f;
			float d[] = {
				distance( pt, vec3( a.x, a.y, a.z ) ),
				distance( pt, vec3( a.x, a.y, b.z ) ),
				distance( pt, vec3( a.x, b.y, a.z ) ),
				distance( pt, vec3( b.x, a.y, a.z ) ),
				distance( pt, vec3( a.x, b.y, b.z ) ),
				distance( pt, vec3( b.x, b.y, a.z ) ),
				distance( pt, vec3( b.x, a.y, b.z ) ),
				distance( pt, vec3( b.x, b.y, b.z ) ),
			};
			float res = INFINITY;
			for ( int i = 0; i < 8; ++i ) {
				res = min( res, d[ i ] );
			}
			return res;
		}
		
	public:
		virtual DbufRtRenderCtx *create_dbuf_rt_render_ctx()
		{
			return new DbufRtRenderCtx;
		}

		virtual std::size_t dbuf_rt_render_frame( Image<cufx::StdByte3Pixel> &frame,
										   DbufRtRenderCtx &ctx,
										   IRenderLoop &loop,
										   OctreeCuller &culler,
										   MpiComm const &comm,
										   std::vector<int> const &z_order ) = 0;

	protected:
		std::shared_ptr<vol::Thumbnail<int>> chebyshev_thumb;
	};
}

VM_END_MODULE()
