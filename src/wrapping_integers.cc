#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + static_cast<uint32_t>( n );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Get the offset from zero_point to this sequence number
  uint64_t offset = raw_value_ - zero_point.raw_value_;
  
  // Start with the candidate that's closest to checkpoint in the same 2^32 range
  uint64_t candidate = ( ( checkpoint / ( 1ULL << 32 ) ) * ( 1ULL << 32 ) ) + offset;
  
  // Consider the candidates above and below
  uint64_t lower = ( candidate >= ( 1ULL << 32 ) ) ? candidate - ( 1ULL << 32 ) : candidate;
  uint64_t upper = candidate + ( 1ULL << 32 );
  
  // Calculate distances to checkpoint
  auto distance = []( uint64_t a, uint64_t b ) { return ( a > b ) ? a - b : b - a; };
  
  uint64_t dist_candidate = distance( candidate, checkpoint );
  uint64_t dist_lower = distance( lower, checkpoint );
  uint64_t dist_upper = distance( upper, checkpoint );
  
  // Return the closest candidate
  if ( dist_lower < dist_candidate && dist_lower < dist_upper ) {
    return lower;
  }
  if ( dist_upper < dist_candidate ) {
    return upper;
  }
  return candidate;
}
