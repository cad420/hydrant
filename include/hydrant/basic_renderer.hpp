#pragma once

#include <varch/thumbnail.hpp>
#include <cudafx/device.hpp>
#include <hydrant/bridge/image.hpp>
#include <hydrant/core/renderer.hpp>
#include <hydrant/bridge/const_texture_3d.hpp>

VM_BEGIN_MODULE( hydrant )

VM_EXPORT
{
	template <typename Shader>
	struct BasicRenderer;

	template <typename T>
	struct ThumbnailTexture : ConstTexture3D<T>
	{
		using ConstTexture3D<T>::ConstTexture3D;

	private:
		std::shared_ptr<vol::Thumbnail<T>> thumb;
		template <typename Shader>
		friend struct BasicRenderer;
	};

	struct BasicRendererParams : vm::json::Serializable<BasicRendererParams>
	{
		VM_JSON_FIELD( ShadingDevice, device ) = ShadingDevice::Cuda;
	};

	template <typename Shader>
	struct BasicRenderer : IRenderer
	{
		bool init( std::shared_ptr<Dataset> const &dataset,
				   RendererConfig const &cfg ) override
		{
			if ( !IRenderer::init( dataset, cfg ) ) { return false; }

			auto params = cfg.params.get<BasicRendererParams>();
			if ( params.device == ShadingDevice::Cuda ) {
				device = cufx::Device::get_default();
				if ( !device.has_value() ) {
					vm::println( "cuda device not found, fallback to cpu render mode" );
				}
			}

			auto img_opts = ImageOptions{}
							  .set_device( device )
							  .set_resolution( cfg.resolution );
			film = Image<typename Shader::Pixel>( img_opts );

			auto &lvl0 = dataset->meta.sample_levels[ 0 ];
			dim = vec3( lvl0.archives[ 0 ].dim.x,
						lvl0.archives[ 0 ].dim.y,
						lvl0.archives[ 0 ].dim.z );
			auto raw = vec3( lvl0.raw.x,
							 lvl0.raw.y,
							 lvl0.raw.z );
			auto f_dim = raw / float( lvl0.archives[ 0 ].block_size );
			exhibit = Exhibit{}
						.set_center( f_dim / 2.f )
						.set_size( f_dim );

			shader.bbox = Box3D{ { 0, 0, 0 }, f_dim };
			shader.step = 1e-2f * f_dim.x / 4.f;

			return true;
		}

	public:
		template <typename T>
		ThumbnailTexture<T> load_thumbnail( std::string const &path )
		{
			std::shared_ptr<vol::Thumbnail<T>> thumb( new vol::Thumbnail<T>( path ) );
			auto opts = ConstTexture3DOptions{}
						  .set_device( device )
						  .set_dim( thumb->dim.x, thumb->dim.y, thumb->dim.z )
						  .set_data( thumb->data() )
						  .set_opts( cufx::Texture::Options::as_array()
									   .set_address_mode( cufx::Texture::AddressMode::Clamp ) );
			ThumbnailTexture<T> texture( opts );
			if ( !device.has_value() ) { texture.thumb = thumb; }
			return texture;
		}

	protected:
		vm::Option<cufx::Device> device;
		uvec3 dim;
		Shader shader;
		Exhibit exhibit;
		Image<typename Shader::Pixel> film;
	};
}

VM_END_MODULE()
