#include <stdexcept>

#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{
  if ( closed_ || error_ ) {
    return;
  }
  
  uint64_t available = available_capacity();
  if ( data.size() > available ) {
    data = data.substr( 0, available );
  }
  
  buffer_ += data;
  bytes_pushed_ += data.size();
}

void Writer::close()
{
  closed_ = true;
}

void Writer::set_error()
{
  error_ = true;
}

bool Writer::is_closed() const
{
  return closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - buffer_.size();
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

string_view Reader::peek() const
{
  return string_view( buffer_ );
}

bool Reader::is_finished() const
{
  return closed_ && buffer_.empty();
}

bool Reader::has_error() const
{
  return error_;
}

void Reader::pop( uint64_t len )
{
  uint64_t bytes_to_pop = min( len, buffer_.size() );
  buffer_.erase( 0, bytes_to_pop );
  bytes_popped_ += bytes_to_pop;
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_.size();
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}
