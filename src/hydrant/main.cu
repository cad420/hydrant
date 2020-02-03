#include <fstream>
#include <atomic>
#include <VMUtils/timer.hpp>
#include <VMUtils/cmdline.hpp>
#include <varch/utils/io.hpp>
#include <varch/unarchive/unarchiver.hpp>
#include <cudafx/transfer.hpp>
#include <thumbnail.hpp>
#include "shaders/scratch.hpp"
#include "shaders/volume_shader.hpp"

using namespace std;
using namespace vol;
using namespace hydrant;

int main( int argc, char **argv )
{
	cmdline::parser a;
	a.add<string>( "in", 'i', "input filename", true );
	a.add<string>( "out", 'o', "output filename", true );
	a.add( "thumb", 't', "take snapshots of single thumbnail file" );
	a.add<string>( "config", 'c', "config file", false );
	a.add<float>( "x", 'x', "camera.x", false, 3 );
	a.add<float>( "y", 'y', "camera.y", false, 2 );
	a.add<float>( "z", 'z', "camera.z", false, 2 );

	a.parse_check( argc, argv );

	auto in = a.get<string>( "in" );
	auto out = a.get<string>( "out" );

	ifstream is( in, ios::ate | ios::binary );
	auto len = is.tellg();
	StreamReader reader( is, 0, len );
	Unarchiver unarchiver( reader );
	Thumbnail<ThumbUnit> thumbnail( in + ".thumb" );

	using Shader = VolumnRayEmitShader;
	Shader shader;

	glm::vec3 min = { 0, 0, 0 };
	glm::vec3 max = { thumbnail.dim.x, thumbnail.dim.y, thumbnail.dim.z };
	auto exhibit = Exhibit{}
					 .set_center( max / 2.f )
					 .set_size( max );

	shader.bbox = Box3D{ min, max };
	shader.th_4 = thumbnail.dim.x / 4.f;

	auto device = cufx::Device::scan()[ 0 ];
	auto thumbnail_extent = cufx::Extent{}
							  .set_width( thumbnail.dim.x )
							  .set_height( thumbnail.dim.y )
							  .set_depth( thumbnail.dim.z );
	auto thumbnail_arr = device.alloc_arraynd<float2, 3>( thumbnail_extent );
	// vm::println( "dim = {}, thumbnail_extent = {}", thumbnail.dim, thumbnail_extent );
	auto view_info = cufx::MemoryView2DInfo{}
					   .set_stride( thumbnail.dim.x * sizeof( float2 ) )
					   .set_width( thumbnail.dim.x )
					   .set_height( thumbnail.dim.y );
	cufx::MemoryView3D<float2> thumbnail_view( thumbnail.data(), view_info, thumbnail_extent );
	cufx::memory_transfer( thumbnail_arr, thumbnail_view ).launch();
	auto tex_opts = cufx::Texture::Options{}
					  .set_address_mode( cufx::Texture::AddressMode::Border )
					  .set_filter_mode( cufx::Texture::FilterMode::None )
					  .set_read_mode( cufx::Texture::ReadMode::Raw )
					  .set_normalize_coords( false );
	cufx::Texture thumbnail_texture( thumbnail_arr, tex_opts );
	shader.thumbnail_tex = thumbnail_texture;

	auto camera = Camera{};
	if ( a.exist( "config" ) ) {
		auto cfg = a.get<string>( "config" );
		camera = Camera::from_config( cfg );
	} else {
		auto x = a.get<float>( "x" );
		auto y = a.get<float>( "y" );
		auto z = a.get<float>( "z" );
		camera.set_position( x, y, z );
	}

	cufx::Image<typename Shader::Pixel> image( 512, 512 );
	auto device_swap = device.alloc_image_swap( image );
	auto img_view = image.view().with_device_memory( device_swap.second );
	img_view.copy_to_device().launch();

	auto wg_cnt = 32 * 32;
	shader.wg_max_emit_cnt = 8;
	shader.wg_len_bytes = sizeof(int) + 
						  shader.wg_max_emit_cnt * sizeof( glm::uvec3 );
	auto global = device.alloc_global( shader.wg_len_bytes * wg_cnt );
	vector<char> absent( global.size() );
	shader.absent_buf = global.view_1d<char>( global.size() );

	Raycaster raycaster;
	{
		std::atomic_uint64_t total_steps( 0 );
		vm::Timer::Scoped timer( [&]( auto dt ) {
			vm::println( "time: {}   avg_step: {}",
						 dt.ms(), total_steps.load() / image.get_width() / image.get_height() );
		} );

		raycaster.cast( exhibit, camera, img_view, shader );

		cufx::memory_transfer( cufx::MemoryView1D<char>( absent ), shader.absent_buf ).launch();
		vector<Idx> block_idxs;

		for ( int i = 0; i != wg_cnt; ++i ) {
			auto wg_base_ptr = absent.data() + i * shader.wg_len_bytes;
			int wg_emit_cnt = *(int*)wg_base_ptr;
			glm::uvec3 *wg_ptr = (glm::uvec3 *)(wg_base_ptr + sizeof(int));
			for ( int j = 0; j != wg_emit_cnt; ++j ) {
				block_idxs.emplace_back( Idx{}
										  .set_x( wg_ptr[ j ].x )
										  .set_y( wg_ptr[ j ].y )
										  .set_z( wg_ptr[ j ].z ) );
			}
			// if ( wg_emit_cnt ) {
			// 	vm::println( "#{} : wg_emit_cnt = {}", i, wg_emit_cnt );
			// 		vm::println( "{}", wg_ptr[ j ] );
			// 	}
			// }
		}
		std::sort( block_idxs.begin(), block_idxs.end() );
		auto last = std::unique( block_idxs.begin(), block_idxs.end() );
		block_idxs.erase( last, block_idxs.end() );
	
		// for (int j = 0; j != block_idxs.size(); ++j) {
		// 	vm::println("{}", block_idxs[j]);
		// }
	}

	img_view.copy_from_device().launch();

	image.dump( out );
}
