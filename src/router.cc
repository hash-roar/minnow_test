#include "router.hh"

#include <iostream>
#include <limits>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  // Add the route to the routing table
  routing_table_.emplace_back(route_prefix, prefix_length, next_hop, interface_num);
}

namespace {
  // Helper function to check if destination matches route
  bool matches_route(uint32_t destination, uint32_t route_prefix, uint8_t prefix_length) {
    if (prefix_length == 0) {
      return true; // Default route matches everything
    }
    auto mask = (0xFFFFFFFFU << (32U - prefix_length));
    return (destination & mask) == (route_prefix & mask);
  }
} // anonymous namespace

void Router::route() {
  // Process datagrams from each interface
  for (auto& interface : interfaces_) {
    // Receive all available datagrams from this interface
    while (true) {
      auto optional_dgram = interface.maybe_receive();
      if (!optional_dgram.has_value()) {
        break; // No more datagrams on this interface
      }
      
      auto& dgram = optional_dgram.value();
      
      // Decrement TTL
      if (dgram.header.ttl <= 1) {
        // TTL was zero or becomes zero after decrement, drop the datagram
        continue;
      }
      dgram.header.ttl--;
      
      // Recompute checksum after TTL change
      dgram.header.compute_checksum();
      
      // Find the best matching route using longest-prefix match
      const RouteEntry* best_route = nullptr;
      uint8_t longest_prefix = 0;
      
      for (const auto& route : routing_table_) {
        if (matches_route(dgram.header.dst, route.route_prefix, route.prefix_length) 
            && route.prefix_length >= longest_prefix) {
          best_route = &route;
          longest_prefix = route.prefix_length;
        }
      }
      
      // If no route matched, drop the datagram
      if (best_route == nullptr) {
        continue;
      }
      
      // Determine the next hop address
      Address next_hop_addr = best_route->next_hop.has_value() 
        ? best_route->next_hop.value()
        : Address::from_ipv4_numeric(dgram.header.dst);
      
      // Send the datagram on the appropriate interface
      interfaces_[best_route->interface_num].send_datagram(dgram, next_hop_addr);
    }
  }
}
