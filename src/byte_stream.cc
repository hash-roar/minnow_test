#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ), buffer_( capacity ) {}

void Writer::push( string data )
{
  if ( closed_ || error_ ) {
    return;
  }
  
  
  auto len_to_write = std::min(data.length(), available_capacity());
  if (len_to_write == 0) {
    return;
  }
  auto tail = (head_ + size_) % capacity_;
  auto first_chunk = std::min(len_to_write, capacity_ - tail);
  std::copy_n(data.begin(), first_chunk, buffer_.begin() + static_cast<int64_t>(tail));
  if (first_chunk < len_to_write) {
    std::copy_n(data.begin() + static_cast<int64_t>(first_chunk), len_to_write - first_chunk, buffer_.begin());
  }
  size_ += len_to_write;
  bytes_pushed_ += len_to_write;
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
  return capacity_ - size_;
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

string_view Reader::peek() const
{
  auto tail_bytes = std::min(size_, capacity_ - head_);
  return {buffer_.data() + head_, tail_bytes};

}

bool Reader::is_finished() const
{
  return closed_ && size_ == 0;
}

bool Reader::has_error() const
{
  return error_;
}

void Reader::pop( uint64_t len )
{ 
  auto len_to_pop = std::min(len, size_);
  head_ = (head_ + len_to_pop) % capacity_;
  size_ -= len_to_pop;
  bytes_popped_ += len_to_pop;
}

uint64_t Reader::bytes_buffered() const
{
  return size_;
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}
