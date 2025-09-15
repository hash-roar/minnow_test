#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message, Reassembler& reassembler, Writer& inbound_stream )
{
  // Set the Initial Sequence Number if this is the first SYN segment
  if ( message.SYN && !isn_.has_value() ) {
    isn_ = message.seqno;
  }
  
  // Only process the message if we have received the ISN
  if ( !isn_.has_value() ) {
    return;
  }
  
  // Convert sequence number to absolute sequence number
  // Use the bytes pushed so far as checkpoint for unwrapping
  uint64_t checkpoint = inbound_stream.bytes_pushed();
  uint64_t abs_seqno = message.seqno.unwrap( isn_.value(), checkpoint );
  uint64_t abs_isn = 0;
  
  // Calculate stream index: absolute seqno minus ISN minus 1 (for SYN)
  // But if this segment itself has SYN, its payload starts at index 0
  uint64_t stream_index = abs_seqno - abs_isn - 1;
  if ( message.SYN ) {
    stream_index = 0;
  }
  
  // Insert the payload into the reassembler
  reassembler.insert( stream_index, message.payload.release(), message.FIN, inbound_stream );
}

TCPReceiverMessage TCPReceiver::send( const Writer& inbound_stream ) const
{
  TCPReceiverMessage result;
  
  // Set window size to the available capacity
  result.window_size = inbound_stream.available_capacity() > UINT16_MAX ? UINT16_MAX : inbound_stream.available_capacity();
  
  // Only set ackno if we have received the ISN
  if ( isn_.has_value() ) {
    // The ackno is the next sequence number we need
    // This is ISN + 1 (for SYN) + bytes_pushed (for data) + (1 if stream is closed, for FIN)
    uint64_t next_abs_seqno =  1 + inbound_stream.bytes_pushed();
    
    // Add 1 more if the stream is closed (for FIN)
    if ( inbound_stream.is_closed() ) {
      next_abs_seqno += 1;
    }
    
    // Wrap it back to a 32-bit sequence number
    result.ackno = Wrap32::wrap( next_abs_seqno, isn_.value() );
  }
  
  return result;
}
