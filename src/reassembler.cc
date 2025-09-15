#include "reassembler.hh"
#include <algorithm>
#include <vector>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring, Writer& output )
{ // Handle the last substring flag
  if ( is_last_substring ) {
    is_last_substring_received_ = true;
    stream_end_index_ = first_index + data.size();
  }

  // If the data starts beyond our capacity window, discard it entirely
  uint64_t max_acceptable_index = next_expected_index_ + output.available_capacity();

  // Trim data that extends beyond our capacity window
  uint64_t last_index = first_index + data.size();
  if ( last_index > max_acceptable_index ) {
    data = data.substr( 0, max_acceptable_index - first_index );
    last_index = max_acceptable_index;
  }

  // If the data ends before our next expected index, it's all redundant
  if ( last_index <= next_expected_index_ ) {
    // Check if we should close the stream
    if ( is_last_substring_received_ && next_expected_index_ >= stream_end_index_ ) {
      output.close();
    }
    return;
  }

  // Trim redundant prefix (data that we've already processed)
  if ( first_index < next_expected_index_ ) {
    uint64_t trim_amount = next_expected_index_ - first_index;
    data = data.substr( trim_amount );
    first_index = next_expected_index_;
  }

  // Now we have data that's at least partially useful
  // We need to merge it with existing unassembled substrings to avoid storing overlaps

  // Find overlapping and adjacent ranges and merge them
  auto insert_start = first_index;
  auto insert_end = first_index + data.size();
  string merged_data = data;

  // Find the range of existing substrings that overlap or are adjacent to our new data
  auto it_start = unassembled_substrings_.lower_bound( insert_start );
  if ( it_start != unassembled_substrings_.begin() ) {
    --it_start;
    // Check if this segment overlaps or is adjacent
    uint64_t seg_start = it_start->first;
    uint64_t seg_end = seg_start + it_start->second.size();
    if ( seg_end < insert_start ) {
      ++it_start; // No overlap or adjacency
    }
  }

  // Include segments that start at or before insert_end (to catch adjacent segments)
  auto it_end = unassembled_substrings_.upper_bound( insert_end );

  // Collect all overlapping/adjacent segments plus the new data
  struct Segment {
    uint64_t start;
    uint64_t end;
    string data;
  };
  
  vector<Segment> segments;
  segments.push_back( { insert_start, insert_end, merged_data } );
  
  for ( auto it = it_start; it != it_end; ++it ) {
    uint64_t seg_start = it->first;
    uint64_t seg_end = seg_start + it->second.size();
    segments.push_back( { seg_start, seg_end, it->second } );
  }
  
  // Sort segments by start position
  std::ranges::sort( segments, []( const Segment& a, const Segment& b ) {
    return a.start < b.start;
  } );
  
  // Find the overall range
  uint64_t final_start = segments[0].start;
  uint64_t final_end = segments[0].end;
  for ( const auto& seg : segments ) {
    final_end = std::max( final_end, seg.end );
  }
  
  // Build the final merged data
  string final_data;
  final_data.resize( final_end - final_start );
  
  // Copy data from each segment to the appropriate position
  for ( const auto& seg : segments ) {
    uint64_t offset = seg.start - final_start;
    std::ranges::copy( seg.data, final_data.begin() + static_cast<ptrdiff_t>( offset ) );
  }
  
  merged_data = std::move( final_data );
  insert_start = final_start;
  insert_end = final_end;

  // Remove the overlapping segments
  unassembled_substrings_.erase( it_start, it_end );

  // Try to write immediately if this data starts at our next expected index
  if ( insert_start == next_expected_index_ ) {
    // Write as much as we can
    output.push( merged_data );
    next_expected_index_ += merged_data.size();

    // Check if we can write more from stored segments
    while ( !unassembled_substrings_.empty() ) {
      auto it = unassembled_substrings_.begin();
      if ( it->first == next_expected_index_ ) {
        // This segment is next in sequence
        output.push( it->second );
        next_expected_index_ += it->second.size();
        unassembled_substrings_.erase( it );
      } else {
        break; // There's a gap
      }
    }
  } else {
    // Store the merged data for later
    unassembled_substrings_[insert_start] = merged_data;
  }

  // Check if we should close the stream
  if ( is_last_substring_received_ && next_expected_index_ >= stream_end_index_ ) {
    output.close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  uint64_t total = 0;
  for ( const auto& segment : unassembled_substrings_ ) {
    total += segment.second.size();
  }
  return total;
}
