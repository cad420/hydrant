#include <fstream>
#include <atomic>
#include <cstdlib>
#include <glog/logging.h>
#include <cppfs/fs.h>
#include <cppfs/FileHandle.h>
#include <cppfs/FilePath.h>
#include <VMUtils/fmt.hpp>
#include <VMUtils/timer.hpp>
#include <VMUtils/cmdline.hpp>
#include <hydrant/core/renderer.hpp>
#include <hydrant/glfw_render_loop.hpp>

using namespace std;
using namespace vol;
using namespace hydrant;
using namespace cppfs;

inline void ensure_dir( std::string const &path_v )
{
	auto path = cppfs::fs::open( path_v );
	if ( !path.exists() ) {
		LOG( FATAL ) << vm::fmt( "the specified path '{}' doesn't exist",
								 path_v );
	} else if ( !path.isDirectory() ) {
		LOG( FATAL ) << vm::fmt( "the specified path '{}' is not a directory",
								 path_v );
	}
}

inline void ensure_file( std::string const &path_v )
{
	auto path = cppfs::fs::open( path_v );
	if ( !path.exists() ) {
		LOG( FATAL ) << vm::fmt( "the specified path '{}' doesn't exist",
								 path_v );
	} else if ( !path.isFile() ) {
		LOG( FATAL ) << vm::fmt( "the specified path '{}' is not a file",
								 path_v );
	}
}

struct Config : vm::json::Serializable<Config>
{
	VM_JSON_FIELD( CameraConfig, camera );
	VM_JSON_FIELD( RendererConfig, render );
};

struct DebGlfwRenderLoop : GlfwRenderLoop
{
	using GlfwRenderLoop::GlfwRenderLoop;

	void on_mouse_button( int button, int action, int mode ) override
	{
		if ( button == GLFW_MOUSE_BUTTON_LEFT ) {
			trackball_rec = action == GLFW_PRESS;
		}
	}

	void on_cursor_pos( double x1, double y1 ) override
	{
		if ( trackball_rec ) {
			orbit.arm.x += -( x1 - x0 ) / resolution.x * 90;
			orbit.arm.y += ( y1 - y0 ) / resolution.y * 90;
			orbit.arm.y = glm::clamp( orbit.arm.y, -90.f, 90.f );
		}
		x0 = x1;
		y0 = y1;
	}

	void on_scroll( double dx, double dy ) override
	{
		orbit.arm.z += dy * ( -1e-1 );
		orbit.arm.z = glm::clamp( orbit.arm.z, .1f, 10.f );
	}

	void post_frame() override
	{
		GlfwRenderLoop::post_frame();
		camera.update_params( orbit );
	}

	void on_frame( cufx::Image<> &frame ) override
	{
		GlfwRenderLoop::on_frame( frame );
		frames += 1;
		auto time = glfwGetTime();
		if ( isnan( prev ) ) {
			prev = time;
		} else if ( time - prev >= 1.0 ) {
			vm::println( "fps: {}", frames );
			frames = 0;
			prev = time;
		}
	}

public:
	double prev = NAN;
	int frames = 0;
	bool trackball_rec = false;
	double x0, y0;
	CameraOrbit orbit;
};

template <typename Enum>
struct OptReader
{
	Enum operator()( const std::string &str )
	{
		return Enum::_from_string( str.c_str() );
	}
};

int main( int argc, char **argv )
{
	google::InitGoogleLogging( argv[ 0 ] );
	
	cmdline::parser a;
	a.add<string>( "in", 'i', "input directory", true );
	a.add<string>( "out", 'o', "output filename", false );
	a.add<string>( "config", 'c', "config file path", true );
	a.add<RealtimeRenderQuality>( "quality", 'q', "rt render quality", false,
								  RealtimeRenderQuality::Dynamic,
								  OptReader<RealtimeRenderQuality>() );
	a.add( "rt", 0, "real time render" );

	a.parse_check( argc, argv );

	auto in = FilePath( a.get<string>( "in" ) );
	ensure_dir( in.resolved() );

	auto cfg_path = FilePath( a.get<string>( "config" ) );
	ensure_file( cfg_path.resolved() );
	ifstream is( cfg_path.resolved() );
	Config cfg;
	is >> cfg;

	RendererFactory factory( in );
	auto renderer = factory.create( cfg.render );

	if ( !a.exist( "rt" ) ) {
		auto out = FilePath( a.get<string>( "out" ) );

		vm::Timer::Scoped _( []( auto dt ) {
			vm::println( "time: {}", dt.ms() );
		} );

		renderer->offline_render( cfg.camera ).dump( out.resolved() );
	} else {
		auto opts = GlfwRenderLoopOptions{}
					  .set_resolution( cfg.render.resolution.x,
									   cfg.render.resolution.y )
					  .set_title( "hydrant" );
		DebGlfwRenderLoop loop( opts, cfg.camera );
		loop.orbit = *cfg.camera.orbit;

		renderer->realtime_render(
		  loop,
		  RealtimeRenderOptions{}
			.set_quality( a.get<RealtimeRenderQuality>( "quality" ) ) );
	}
}
