#include "net/DnsMessage.h"

#include <cstring>

namespace ledframe {

namespace {

constexpr size_t kHeaderSize = 12;
constexpr uint16_t kTypeA = 1;
constexpr uint16_t kClassIn = 1;
constexpr uint32_t kTtlS = 10;  // short: a phone that leaves the portal asks again soon

uint16_t get16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

void put16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value >> 8);
  p[1] = static_cast<uint8_t>(value & 0xFF);
}

}  // namespace

size_t buildDnsReply(const uint8_t* query, size_t length, const uint8_t (&ipv4)[4], uint8_t* out,
                     size_t capacity, DnsQuestion& question) {
  question = DnsQuestion{};
  if (query == nullptr || out == nullptr || length < kHeaderSize + 5) {
    return 0;
  }
  const uint16_t flags = get16(query + 2);
  const bool isQuery = (flags & 0x8000) == 0;
  const uint8_t opcode = static_cast<uint8_t>((flags >> 11) & 0x0F);
  if (!isQuery || opcode != 0 || get16(query + 4) != 1) {
    return 0;  // responses, other opcodes and several questions are ignored
  }

  // QNAME: labels up to a zero byte; compression is not allowed in a question.
  size_t pos = kHeaderSize;
  size_t nameLength = 0;
  while (pos < length && query[pos] != 0) {
    const uint8_t label = query[pos];
    if ((label & 0xC0) != 0 || pos + 1 + label > length) {
      return 0;
    }
    for (uint8_t i = 0; i < label; ++i) {
      if (nameLength + 2 < sizeof(question.name)) {
        if (nameLength > 0 && i == 0) {
          question.name[nameLength++] = '.';
        }
        question.name[nameLength++] = static_cast<char>(query[pos + 1 + i]);
      }
    }
    pos += 1 + label;
  }
  question.name[nameLength] = '\0';
  if (pos + 5 > length) {
    return 0;  // no room for the zero byte, QTYPE and QCLASS
  }
  const size_t questionEnd = pos + 5;
  question.type = get16(query + pos + 1);
  const bool answer = question.type == kTypeA && get16(query + pos + 3) == kClassIn;

  const size_t replyLength = questionEnd + (answer ? 16 : 0);
  if (capacity < replyLength) {
    return 0;
  }
  std::memcpy(out, query, questionEnd);  // id, flags, counts and the question
  // QR, opcode 0, AA, RD copied; RA; RCODE 0 (NOERROR).
  put16(out + 2, static_cast<uint16_t>(0x8400 | (flags & 0x0100) | 0x0080));
  put16(out + 6, answer ? 1 : 0);  // ANCOUNT
  put16(out + 8, 0);               // NSCOUNT
  put16(out + 10, 0);              // ARCOUNT: the EDNS record of the query is dropped
  if (answer) {
    uint8_t* a = out + questionEnd;
    put16(a, 0xC000 | kHeaderSize);  // name: pointer to the question
    put16(a + 2, kTypeA);
    put16(a + 4, kClassIn);
    put16(a + 6, 0);
    put16(a + 8, static_cast<uint16_t>(kTtlS));
    put16(a + 10, 4);
    std::memcpy(a + 12, ipv4, 4);
  }
  return replyLength;
}

}  // namespace ledframe
