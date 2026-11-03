#include <unity.h>

#include <cstring>
#include <vector>

#include "net/DnsMessage.h"

using ledframe::DnsQuestion;

namespace {

const uint8_t kAddress[4] = {192, 168, 4, 1};

// A query for name with type, optionally with an EDNS OPT record like Android sends.
std::vector<uint8_t> query(const char* name, uint16_t type, bool edns = false) {
  std::vector<uint8_t> q = {0xAB, 0xCD, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, static_cast<uint8_t>(edns ? 1 : 0)};
  const char* start = name;
  for (const char* c = name;; ++c) {
    if (*c == '.' || *c == '\0') {
      q.push_back(static_cast<uint8_t>(c - start));
      q.insert(q.end(), start, c);
      if (*c == '\0') {
        break;
      }
      start = c + 1;
    }
  }
  q.push_back(0);
  q.push_back(static_cast<uint8_t>(type >> 8));
  q.push_back(static_cast<uint8_t>(type & 0xFF));
  q.push_back(0);
  q.push_back(1);  // class IN
  if (edns) {
    const uint8_t opt[] = {0, 0, 41, 0x04, 0xD0, 0, 0, 0, 0, 0, 0};
    q.insert(q.end(), opt, opt + sizeof(opt));
  }
  return q;
}

uint16_t at16(const uint8_t* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_a_query_gets_the_device_address() {
  const std::vector<uint8_t> q = query("connectivitycheck.gstatic.com", 1);
  uint8_t reply[600];
  DnsQuestion question;
  const size_t length = ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question);
  TEST_ASSERT_EQUAL_UINT32(q.size() + 16, length);
  TEST_ASSERT_EQUAL_STRING("connectivitycheck.gstatic.com", question.name);
  TEST_ASSERT_EQUAL_UINT16(1, question.type);
  TEST_ASSERT_EQUAL_HEX16(0xABCD, at16(reply));       // same id
  TEST_ASSERT_TRUE((reply[2] & 0x80) != 0);           // response
  TEST_ASSERT_TRUE((reply[2] & 0x01) != 0);           // RD copied
  TEST_ASSERT_EQUAL_UINT8(0, reply[3] & 0x0F);        // NOERROR
  TEST_ASSERT_EQUAL_UINT16(1, at16(reply + 4));       // one question
  TEST_ASSERT_EQUAL_UINT16(1, at16(reply + 6));       // one answer
  TEST_ASSERT_EQUAL_UINT8_ARRAY(q.data() + 12, reply + 12, q.size() - 12);  // question copied
  const uint8_t* answer = reply + q.size();
  TEST_ASSERT_EQUAL_HEX16(0xC00C, at16(answer));      // name pointer
  TEST_ASSERT_EQUAL_UINT16(1, at16(answer + 2));      // A
  TEST_ASSERT_EQUAL_UINT16(4, at16(answer + 10));     // 4 bytes
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kAddress, answer + 12, 4);
}

void test_edns_query_is_answered_without_the_opt_record() {
  const std::vector<uint8_t> q = query("www.google.com", 1, true);
  uint8_t reply[600];
  DnsQuestion question;
  const size_t length = ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question);
  TEST_ASSERT_EQUAL_UINT32(q.size() - 11 + 16, length);  // the OPT record is not copied
  TEST_ASSERT_EQUAL_UINT16(0, at16(reply + 10));          // ARCOUNT 0
  TEST_ASSERT_EQUAL_UINT16(1, at16(reply + 6));
  TEST_ASSERT_EQUAL_STRING("www.google.com", question.name);
}

void test_aaaa_query_gets_an_empty_answer() {
  const std::vector<uint8_t> q = query("captive.apple.com", 28, true);
  uint8_t reply[600];
  DnsQuestion question;
  const size_t length = ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question);
  TEST_ASSERT_EQUAL_UINT32(q.size() - 11, length);
  TEST_ASSERT_EQUAL_UINT16(28, question.type);
  TEST_ASSERT_EQUAL_UINT16(0, at16(reply + 6));     // no answer
  TEST_ASSERT_EQUAL_UINT8(0, reply[3] & 0x0F);      // NOERROR, not NXDOMAIN
}

void test_invalid_packets_get_no_reply() {
  uint8_t reply[600];
  DnsQuestion question;
  std::vector<uint8_t> q = query("example.com", 1);
  q[2] |= 0x80;  // a response, not a query
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question));
  q = query("example.com", 1);
  q[5] = 2;  // two questions
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question));
  q = query("example.com", 1);
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::buildDnsReply(q.data(), q.size() - 3, kAddress, reply, sizeof(reply), question));
  q[12] = 0xC0;  // compression in the question
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question));
  q = query("example.com", 1);
  q[12] = 60;  // label longer than the packet
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question));
  q = query("example.com", 1);
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, 10, question));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::buildDnsReply(nullptr, 40, kAddress, reply, sizeof(reply), question));
}

void test_long_names_are_cut_in_the_log_copy() {
  const std::vector<uint8_t> q = query(
      "a123456789.b123456789.c123456789.d123456789.e123456789.f123456789.g123456789.example", 1);
  uint8_t reply[600];
  DnsQuestion question;
  TEST_ASSERT_TRUE(ledframe::buildDnsReply(q.data(), q.size(), kAddress, reply, sizeof(reply), question) > 0);
  TEST_ASSERT_TRUE(std::strlen(question.name) < sizeof(question.name));
  TEST_ASSERT_EQUAL_STRING_LEN("a123456789.b123456789", question.name, 21);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_a_query_gets_the_device_address);
  RUN_TEST(test_edns_query_is_answered_without_the_opt_record);
  RUN_TEST(test_aaaa_query_gets_an_empty_answer);
  RUN_TEST(test_invalid_packets_get_no_reply);
  RUN_TEST(test_long_names_are_cut_in_the_log_copy);
  return UNITY_END();
}
