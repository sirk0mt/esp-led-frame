// Captive portal DNS answers (plain logic, tested natively).
//
// In AP mode every name resolves to the device: an A query gets one A record with
// the device address, any other type (AAAA, HTTPS, ...) an empty NOERROR answer, so
// clients fall back to IPv4. EDNS records in the query are ignored.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ledframe {

struct DnsQuestion {
  char name[64] = "";  // dotted name for logs, cut at 63 characters
  uint16_t type = 0;   // 1 = A, 28 = AAAA, ...
};

// Writes the reply to query into out. ipv4 holds the address bytes in network
// order. Returns the reply length, or 0 for a packet that is not a standard query
// with one question (no reply is sent then). question receives the asked name.
size_t buildDnsReply(const uint8_t* query, size_t length, const uint8_t (&ipv4)[4], uint8_t* out,
                     size_t capacity, DnsQuestion& question);

}  // namespace ledframe
