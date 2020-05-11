#pragma once

#include <VMUtils/concepts.hpp>
#include <hydrant/octree_culler.hpp>

VM_BEGIN_MODULE( hydrant )

VM_EXPORT
{
	struct KdNode
	{
		std::unique_ptr<KdNode> left, right;
		int rank, axis, mid;
		float ratio;

	public:
		void update_by_ratio( BoundingBox const &parent,
							  BoundingBox &left,
							  BoundingBox &right )
		{
			vec3 minp = parent.min;
			vec3 maxp = parent.max;
			vec3 midp_f = minp * ( 1 - ratio ) + maxp * ratio;
			ivec3 midp = round( midp_f );
			switch ( axis ) {
			case 0: mid = left.max.x = right.min.x = midp.x; break;
			case 1: mid = left.max.y = right.min.z = midp.y; break;
			case 2: mid = left.max.z = right.min.z = midp.z; break;
			}
		}
	};

	struct RangeSum
	{
		RangeSum( std::vector<std::size_t> const &vals ) :
			sums( vals.size() + 1 )
		{
			std::size_t sum = 0;
			for ( int i = 0; i != vals.size(); ++i ) {
				sums[ i ] = sum;
				sum += vals[ i ];
			}
			sums[ vals.size() ] = sum;
		}

		std::size_t range_sum( int low, int high ) const
		{
			return sums[ high ] - sums[ low ];
		}
		
	private:
		std::vector<std::size_t> sums;
	};
	
	struct DynKdTree
	{
		DynKdTree( ivec3 const &dim, int cnt ) :
			cnt( cnt ),
			bbox( BoundingBox{}.set_min( 0 ).set_max( dim ) )
		{
			root = split( bbox, 0, cnt );
		}

	public:
		BoundingBox search( int rank ) const
		{
			BoundingBox res = bbox;
			search_impl( root.get(), rank, res );
			return res;
		}

		void update( std::vector<std::size_t> const &render_t )
		{
			RangeSum sum( render_t );
			update_impl( root.get(), sum, bbox, 0, cnt );
		}

	private:
		void update_impl( KdNode *node, RangeSum const &sum,
						  BoundingBox const &bbox, int low, int high )
		{
			if ( node == nullptr ) return;
			
			auto l_t = sum.range_sum( low, node->rank );
			auto r_t = sum.range_sum( node->rank, high );

			const float speed = 0.1;
			double l = node->ratio / double( l_t );
			double r = ( 1 - node->ratio ) / double( r_t );
			node->ratio = speed * l / ( l + r ) + ( 1 - speed ) * node->ratio;
			vm::println( "node.ratio = {}", node->ratio );
			BoundingBox bbox_l, bbox_r;
			node->update_by_ratio( bbox, bbox_l, bbox_r );
			
			update_impl( node->left.get(), sum, bbox_l, low, node->rank );
			update_impl( node->right.get(), sum, bbox_r, node->rank, high );
		}
		
		void search_impl( KdNode *node, int rank, BoundingBox &res ) const
		{
			if ( node == nullptr ) return;

			ivec3 *anch = nullptr;
			KdNode *next = nullptr;
			if ( rank < node->rank ) {
				anch = &res.max;
				next = node->left.get();
			} else {
				anch = &res.min;
				next = node->right.get();
			}
			switch ( node->axis ) {
			case 0: anch->x = node->mid; break;
			case 1: anch->y = node->mid; break;
			case 2: anch->z = node->mid; break;
			}
			search_impl( next, rank, res );
		}

		std::unique_ptr<KdNode> split( BoundingBox const &bbox,
									   int low_rank, int high_rank, int axis = 0 ) {
			std::unique_ptr<KdNode> res;
			if ( high_rank - low_rank > 1 ) {
				auto node = new KdNode;
				auto rank = ( high_rank + low_rank ) / 2;
				auto next_axis = ( axis + 1 ) % 3;
				node->ratio = .5f;
				node->rank = rank;
				node->axis = axis;
				BoundingBox bbox_l, bbox_r;
				node->update_by_ratio( bbox, bbox_l, bbox_r );
				node->left = split( bbox_l, low_rank, rank, next_axis );
				node->right = split( bbox_r, rank, high_rank, next_axis );
				res.reset( node );
			}
			return res;
		}
		
	private:
		int cnt;
		BoundingBox bbox;
		std::unique_ptr<KdNode> root;
	};
}

VM_END_MODULE()
