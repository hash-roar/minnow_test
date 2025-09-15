#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

using namespace std;

// ethernet_address: Ethernet (what ARP calls "hardware") address of the interface
// ip_address: IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( const EthernetAddress& ethernet_address, const Address& ip_address )
  : ethernet_address_( ethernet_address ), ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

// dgram: the IPv4 datagram to be sent
// next_hop: the IP address of the interface to send it to (typically a router or default gateway, but
// may also be another host if directly connected to the same network as the destination)

// Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) by using the
// Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  uint32_t next_hop_ip = next_hop.ipv4_numeric();

  // Check if we already know the Ethernet address for this IP
  auto arp_entry = arp_cache_.find( next_hop_ip );
  if ( arp_entry != arp_cache_.end() ) {
    // We know the Ethernet address, send immediately
    EthernetFrame frame;
    frame.header.dst = arp_entry->second.first;
    frame.header.src = ethernet_address_;
    frame.header.type = EthernetHeader::TYPE_IPv4;
    frame.payload = serialize( dgram );

    frames_to_send_.push( frame );
    return;
  }

  // We don't know the Ethernet address, need to do ARP
  // First, queue the datagram
  pending_datagrams_[next_hop_ip].push( dgram );

  // Check if we've already sent an ARP request recently
  auto pending_request = pending_arp_requests_.find( next_hop_ip );
  if ( pending_request != pending_arp_requests_.end() ) {
    // We've sent a request recently, just wait
    return;
  }

  // Send ARP request
  ARPMessage arp_request;
  arp_request.opcode = ARPMessage::OPCODE_REQUEST;
  arp_request.sender_ethernet_address = ethernet_address_;
  arp_request.sender_ip_address = ip_address_.ipv4_numeric();
  arp_request.target_ethernet_address = {}; // Unknown, that's what we're asking for
  arp_request.target_ip_address = next_hop_ip;

  EthernetFrame arp_frame;
  arp_frame.header.dst = ETHERNET_BROADCAST;
  arp_frame.header.src = ethernet_address_;
  arp_frame.header.type = EthernetHeader::TYPE_ARP;
  arp_frame.payload = serialize( arp_request );

  frames_to_send_.push( arp_frame );
  pending_arp_requests_[next_hop_ip] = current_time_ms_;
}

// frame: the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame( const EthernetFrame& frame )
{
  // Ignore frames not destined for us (unless broadcast)
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST ) {
    return {};
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    // IPv4 datagram
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      return dgram;
    }
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    // ARP message
    ARPMessage arp_message;
    if ( parse( arp_message, frame.payload ) && arp_message.supported() ) {
      // Learn the mapping from the sender
      uint32_t sender_ip = arp_message.sender_ip_address;
      EthernetAddress sender_eth = arp_message.sender_ethernet_address;

      // Update ARP cache
      arp_cache_[sender_ip] = make_pair( sender_eth, current_time_ms_ );

      // Remove pending ARP request if we had one
      pending_arp_requests_.erase( sender_ip );

      // Send any pending datagrams for this IP
      auto pending_it = pending_datagrams_.find( sender_ip );
      if ( pending_it != pending_datagrams_.end() ) {
        while ( !pending_it->second.empty() ) {
          InternetDatagram pending_dgram = pending_it->second.front();
          pending_it->second.pop();

          EthernetFrame eth_frame;
          eth_frame.header.dst = sender_eth;
          eth_frame.header.src = ethernet_address_;
          eth_frame.header.type = EthernetHeader::TYPE_IPv4;
          eth_frame.payload = serialize( pending_dgram );
          frames_to_send_.push( eth_frame );
        }
        pending_datagrams_.erase( pending_it );
      }

      // If this is an ARP request for our IP, send a reply
      if ( arp_message.opcode == ARPMessage::OPCODE_REQUEST
           && arp_message.target_ip_address == ip_address_.ipv4_numeric() ) {
        ARPMessage arp_reply;
        arp_reply.opcode = ARPMessage::OPCODE_REPLY;
        arp_reply.sender_ethernet_address = ethernet_address_;
        arp_reply.sender_ip_address = ip_address_.ipv4_numeric();
        arp_reply.target_ethernet_address = arp_message.sender_ethernet_address;
        arp_reply.target_ip_address = arp_message.sender_ip_address;

        EthernetFrame reply_frame;
        reply_frame.header.dst = arp_message.sender_ethernet_address;
        reply_frame.header.src = ethernet_address_;
        reply_frame.header.type = EthernetHeader::TYPE_ARP;
        reply_frame.payload = serialize( arp_reply );

        frames_to_send_.push( reply_frame );
      }
    }
  }

  return {};
}

// ms_since_last_tick: the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  current_time_ms_ += ms_since_last_tick;

  // Remove expired ARP cache entries (30 seconds)
  auto arp_it = arp_cache_.begin();
  while ( arp_it != arp_cache_.end() ) {
    if ( current_time_ms_ - arp_it->second.second >= ARP_CACHE_TIMEOUT_MS ) {
      arp_it = arp_cache_.erase( arp_it );
    } else {
      ++arp_it;
    }
  }

  // Remove expired pending ARP requests (5 seconds)
  auto pending_it = pending_arp_requests_.begin();
  while ( pending_it != pending_arp_requests_.end() ) {
    if ( current_time_ms_ - pending_it->second >= ARP_REQUEST_TIMEOUT_MS ) {
      pending_it = pending_arp_requests_.erase( pending_it );
    } else {
      ++pending_it;
    }
  }
}

optional<EthernetFrame> NetworkInterface::maybe_send()
{
  if ( frames_to_send_.empty() ) {
    return {};
  }

  EthernetFrame frame = frames_to_send_.front();
  frames_to_send_.pop();
  return frame;
}
