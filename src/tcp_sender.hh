#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <queue>

class TCPSender
{
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

  // TCP sender state
  uint64_t next_seqno_ { 0 };          // Next sequence number to send
  uint64_t bytes_in_flight_ { 0 };     // Number of sequence numbers outstanding
  uint64_t ackd_seqno_ { 0 };          // Last acknowledged sequence number
  uint16_t receiver_window_size_ { 1 }; // Receiver's advertised window size
  bool receiver_has_ackno_ { false };   // Whether we've received an ackno from receiver
  
  // SYN and FIN tracking
  bool syn_sent_ { false };
  bool fin_sent_ { false };
  
  // Retransmission timer
  uint64_t current_RTO_ms_;            // Current retransmission timeout
  uint64_t timer_running_until_ { 0 }; // When the timer expires (0 = not running)
  uint64_t time_elapsed_ { 0 };        // Total time elapsed since construction
  uint64_t consecutive_retx_ { 0 };    // Number of consecutive retransmissions
  
  // Outstanding segments (for retransmission)
  std::queue<TCPSenderMessage> outstanding_segments_;
  // std::queue<uint64_t> outstanding_timestamps_; // When each segment was sent
  
  // Messages ready to send
  std::queue<TCPSenderMessage> messages_to_send_;

  // Helper methods
  void start_timer_if_needed();
  void stop_timer();
  bool timer_expired() const;
  uint64_t window_size() const;

public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( uint64_t initial_RTO_ms, std::optional<Wrap32> fixed_isn );

  /* Push bytes from the outbound stream */
  void push( Reader& outbound_stream );

  /* Send a TCPSenderMessage if needed (or empty optional otherwise) */
  std::optional<TCPSenderMessage> maybe_send();

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage send_empty_message() const;

  /* Receive an act on a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called. */
  void tick( uint64_t ms_since_last_tick );

  /* Accessors for use in testing */
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?
};
