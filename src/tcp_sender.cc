#include "tcp_sender.hh"
#include "tcp_config.hh"

#include <random>

using namespace std;

/* TCPSender constructor (uses a random ISN if none given) */
TCPSender::TCPSender( uint64_t initial_RTO_ms, optional<Wrap32> fixed_isn )
  : isn_( fixed_isn.value_or( Wrap32 { random_device()() } ) )
  , initial_RTO_ms_( initial_RTO_ms )
  , current_RTO_ms_( initial_RTO_ms )
{}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return bytes_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retx_;
}

optional<TCPSenderMessage> TCPSender::maybe_send()
{
  if ( messages_to_send_.empty() ) {
    return {};
  }

  TCPSenderMessage msg = messages_to_send_.front();
  messages_to_send_.pop();
  return msg;
}

void TCPSender::push( Reader& outbound_stream )
{
  // Get effective window size (special case for zero window)
  uint64_t window = window_size();

  // If we haven't sent SYN yet, send it
  if ( !syn_sent_ ) {
    TCPSenderMessage msg;
    msg.seqno = isn_;
    msg.SYN = true;
    msg.payload = Buffer {};
    
    // Check if we should also set FIN (if stream is already finished and we have window space)
    msg.FIN = !fin_sent_ && outbound_stream.is_finished() && window >= 2;
    
    if ( msg.FIN ) {
      fin_sent_ = true;
    }

    syn_sent_ = true;
    bytes_in_flight_ += msg.sequence_length();
    outstanding_segments_.push( msg );
    messages_to_send_.push( msg );
    next_seqno_ += msg.sequence_length();

    start_timer_if_needed();
    return;
  }

  // Send data segments while there's window space and data available
  while ( bytes_in_flight_ < window && outbound_stream.bytes_buffered() > 0 ) {
    TCPSenderMessage msg;
    msg.seqno = isn_ + next_seqno_;
    msg.SYN = false;

    // Calculate available window space
    uint64_t available_space = window - bytes_in_flight_;
    uint64_t bytes_to_send = min(
      { outbound_stream.bytes_buffered(), available_space, static_cast<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE ) } );

    // Read data from stream
    string data;
    read( outbound_stream, bytes_to_send, data );
    msg.payload = Buffer( move( data ) );

    // Check if we should set FIN flag
    msg.FIN = !fin_sent_ && outbound_stream.is_finished() && available_space > msg.payload.size();

    if ( msg.FIN ) {
      fin_sent_ = true;
    }

    // Only send if message has content
    if ( msg.sequence_length() > 0 ) {
      bytes_in_flight_ += msg.sequence_length();
      outstanding_segments_.push( msg );
      messages_to_send_.push( msg );
      next_seqno_ += msg.sequence_length();

      start_timer_if_needed();
    }
  }

  // Send FIN if stream is finished and we haven't sent FIN yet
  if ( !fin_sent_ && outbound_stream.is_finished() && bytes_in_flight_ < window ) {
    TCPSenderMessage msg;
    msg.seqno = isn_ + next_seqno_;
    msg.SYN = false;
    msg.payload = Buffer {};
    msg.FIN = true;

    fin_sent_ = true;
    bytes_in_flight_ += msg.sequence_length();
    outstanding_segments_.push( msg );
    messages_to_send_.push( msg );
    next_seqno_ += msg.sequence_length();

    start_timer_if_needed();
  }
}

TCPSenderMessage TCPSender::send_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = isn_ + next_seqno_;
  msg.SYN = false;
  msg.payload = Buffer {};
  msg.FIN = false;

  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  receiver_window_size_ = msg.window_size;

  if ( !msg.ackno.has_value() ) {
    return;
  }

  uint64_t ackno = msg.ackno.value().unwrap( isn_, next_seqno_ );

  // Ignore if ackno doesn't acknowledge new data or is impossible (beyond next_seqno)
  if ( ackno <= ackd_seqno_ || ackno > next_seqno_ ) {
    return;
  }

  // Update acknowledged sequence number
  ackd_seqno_ = ackno;
  receiver_has_ackno_ = true;

  // Remove acknowledged segments from outstanding queue
  queue<TCPSenderMessage> new_outstanding;
  queue<uint64_t> new_timestamps;

  while ( !outstanding_segments_.empty() ) {
    const auto& seg = outstanding_segments_.front();
    uint64_t seg_start = seg.seqno.unwrap( isn_, ackd_seqno_ );
    uint64_t seg_end = seg_start + seg.sequence_length();

    if ( seg_end <= ackno ) {
      // This segment is fully acknowledged
      bytes_in_flight_ -= seg.sequence_length();
      outstanding_segments_.pop();
    } else {
      // This segment is not fully acknowledged, keep it
      new_outstanding.push( seg );
      outstanding_segments_.pop();
    }
  }

  outstanding_segments_ = move( new_outstanding );

  // Reset RTO and restart timer if we have outstanding data
  current_RTO_ms_ = initial_RTO_ms_;
  consecutive_retx_ = 0;

  if ( !outstanding_segments_.empty() ) {
    timer_running_until_ = time_elapsed_ + current_RTO_ms_;
  } else {
    stop_timer();
  }
}

void TCPSender::tick( const size_t ms_since_last_tick )
{
  time_elapsed_ += ms_since_last_tick;

  if ( timer_expired() && !outstanding_segments_.empty() ) {
    // Retransmit the earliest outstanding segment
    TCPSenderMessage msg = outstanding_segments_.front();
    messages_to_send_.push( msg );

    if ( receiver_window_size_ > 0 ) {
      consecutive_retx_++;
      current_RTO_ms_ *= 2; // Exponential backoff
    }

    // Restart timer
    timer_running_until_ = time_elapsed_ + current_RTO_ms_;
  }
}

// Helper methods
void TCPSender::start_timer_if_needed()
{
  if ( timer_running_until_ == 0 && !outstanding_segments_.empty() ) {
    timer_running_until_ = time_elapsed_ + current_RTO_ms_;
  }
}

void TCPSender::stop_timer()
{
  timer_running_until_ = 0;
}

bool TCPSender::timer_expired() const
{
  return timer_running_until_ > 0 && time_elapsed_ >= timer_running_until_;
}

uint64_t TCPSender::window_size() const
{
  if ( receiver_window_size_ == 0 ) {
    return 1; // Special case: treat zero window as size 1 for probing
  }
  return receiver_window_size_;
}
