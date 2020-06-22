#include "simdjson.h"
#include <cstddef>
#include <cstdint>
#include <random>

class RandomUTF8 final {
public:
  RandomUTF8(std::random_device &rd, int prob_1byte, int prob_2bytes,
             int prob_3bytes, int prob_4bytes);

  std::vector<uint8_t> generate(size_t output_bytes);
  std::vector<uint8_t> generate(size_t output_bytes, long seed);

private:
  uint32_t generate();

  std::mt19937 gen;
  std::discrete_distribution<> bytes_count;
  std::uniform_int_distribution<uint8_t> val_7bit{0x00, 0x7f}; // 0b0xxxxxxx
  std::uniform_int_distribution<uint8_t> val_6bit{0x00, 0x3f}; // 0b10xxxxxx
  std::uniform_int_distribution<uint8_t> val_5bit{0x00, 0x1f}; // 0b110xxxxx
  std::uniform_int_distribution<uint8_t> val_4bit{0x00, 0x0f}; // 0b1110xxxx
  std::uniform_int_distribution<uint8_t> val_3bit{0x00, 0x07}; // 0b11110xxx
};

RandomUTF8::RandomUTF8(std::random_device &rd, int prob_1byte, int prob_2bytes,
                       int prob_3bytes, int prob_4bytes)
    : gen(rd()), bytes_count({double(prob_1byte), double(prob_2bytes),
                              double(prob_3bytes), double(prob_4bytes)}) {}

std::vector<uint8_t> RandomUTF8::generate(size_t output_bytes) {
  std::vector<uint8_t> result;
  result.reserve(output_bytes);
  uint8_t candidate, head;
  while (result.size() < output_bytes) {
    switch (bytes_count(gen)) {
    case 0: // 1 byte
      candidate = val_7bit(gen);
      while (candidate == 0) { // though strictly speaking, a stream of nulls is
                               // UTF8, it tends to break some code
        candidate = val_7bit(gen);
      }
      result.push_back(candidate);
      break;
    case 1: // 2 bytes
      candidate = 0xc0 | val_5bit(gen);
      while (candidate < 0xC2) {
        candidate = 0xc0 | val_5bit(gen);
      }
      result.push_back(candidate);
      result.push_back(0x80 | val_6bit(gen));
      break;
    case 2: // 3 bytes
      head = 0xe0 | val_4bit(gen);
      result.push_back(head);
      candidate = 0x80 | val_6bit(gen);
      if (head == 0xE0) {
        while (candidate < 0xA0) {
          candidate = 0x80 | val_6bit(gen);
        }
      } else if (head == 0xED) {
        while (candidate > 0x9F) {
          candidate = 0x80 | val_6bit(gen);
        }
      }
      result.push_back(candidate);
      result.push_back(0x80 | val_6bit(gen));
      break;
    case 3: // 4 bytes
      head = 0xf0 | val_3bit(gen);
      while (head > 0xF4) {
        head = 0xf0 | val_3bit(gen);
      }
      result.push_back(head);
      candidate = 0x80 | val_6bit(gen);
      if (head == 0xF0) {
        while (candidate < 0x90) {
          candidate = 0x80 | val_6bit(gen);
        }
      } else if (head == 0xF4) {
        while (candidate > 0x8F) {
          candidate = 0x80 | val_6bit(gen);
        }
      }
      result.push_back(candidate);
      result.push_back(0x80 | val_6bit(gen));
      result.push_back(0x80 | val_6bit(gen));
      break;
    }
  }
  result.push_back(0); // EOS for scalar code

  return result;
}

std::vector<uint8_t> RandomUTF8::generate(size_t output_bytes, long seed) {
  gen.seed(uint32_t(seed));
  return generate(output_bytes);
}

WARN_UNUSED bool basic_validate_utf8(const char *buf, size_t len) noexcept {
  const uint8_t *data = (const uint8_t *)buf;
  uint64_t pos = 0;
  uint64_t next_pos = 0;
  uint32_t code_point = 0;
  while (pos < len) {
    unsigned char byte = data[pos];
    if (byte < 0b10000000) {
      pos++;
      continue;
    } else if ((byte & 0b11100000) == 0b11000000) {
      next_pos = pos + 2;
      if (next_pos > len) {
        return false;
      }
      if ((data[pos + 1] & 0b11000000) != 0b10000000) {
        return false;
      }
      // range check
      code_point = (byte & 0b00011111) << 6 | (data[pos + 1] & 0b00111111);
      if (code_point < 0x80 || 0x7ff < code_point) {
        return false;
      }
    } else if ((byte & 0b11110000) == 0b11100000) {
      next_pos = pos + 3;
      if (next_pos > len) {
        return false;
      }
      if ((data[pos + 1] & 0b11000000) != 0b10000000) {
        return false;
      }
      if ((data[pos + 2] & 0b11000000) != 0b10000000) {
        return false;
      }
      // range check
      code_point = (byte & 0b00001111) << 12 |
                   (data[pos + 1] & 0b00111111) << 6 |
                   (data[pos + 2] & 0b00111111);
      if (code_point < 0x800 || 0xffff < code_point ||
          (0xd7ff < code_point && code_point < 0xe000)) {
        return false;
      }
    } else if ((byte & 0b11111000) == 0b11110000) { // 0b11110000
      next_pos = pos + 4;
      if (next_pos > len) {
        return false;
      }
      if ((data[pos + 1] & 0b11000000) != 0b10000000) {
        return false;
      }
      if ((data[pos + 2] & 0b11000000) != 0b10000000) {
        return false;
      }
      if ((data[pos + 3] & 0b11000000) != 0b10000000) {
        return false;
      }
      // range check
      code_point =
          (byte & 0b00000111) << 18 | (data[pos + 1] & 0b00111111) << 12 |
          (data[pos + 2] & 0b00111111) << 6 | (data[pos + 3] & 0b00111111);
      if (code_point < 0xffff || 0x10ffff < code_point) {
        return false;
      }
    } else {
      // we may have a continuation
      return false;
    }
    pos = next_pos;
  }
  return true;
}

void brute_force_tests() {
  printf("running brute-force UTF-8 tests... ");
  fflush(NULL);
  std::random_device rd{};
  RandomUTF8 gen_1_2_3_4(rd, 1, 1, 1, 1);
  size_t total = 1000;
  for (size_t i = 0; i < total; i++) {

    auto UTF8 = gen_1_2_3_4.generate(rand() % 256);
    if (!simdjson::validate_utf8((const char *)UTF8.data(), UTF8.size())) {
      std::cerr << "bug" << std::endl;
      abort();
    }
    for (size_t flip = 0; flip < 1000; ++flip) {
      // we are going to hack the string as long as it is UTF-8
      UTF8[rand() % UTF8.size()] ^= uint8_t(1)
                                    << (rand() % 8); // we flip exactly one bit
      bool is_ok =
          simdjson::validate_utf8((const char *)UTF8.data(), UTF8.size());
      bool is_ok_basic =
          basic_validate_utf8((const char *)UTF8.data(), UTF8.size());
      if (is_ok != is_ok_basic) {
        std::cerr << "bug" << std::endl;
        abort();
      }
    }
  }
  printf("tests ok.\n");
}

void test() {
  printf("running hard-coded UTF-8 tests... ");
  fflush(NULL);
  // additional tests are from autobahn websocket testsuite
  // https://github.com/crossbario/autobahn-testsuite/tree/master/autobahntestsuite/autobahntestsuite/case
  const char *goodsequences[] = {"a",
                                 "\xc3\xb1",
                                 "\xe2\x82\xa1",
                                 "\xf0\x90\x8c\xbc",
                                 "안녕하세요, 세상",
                                 "\xc2\x80",         // 6.7.2
                                 "\xf0\x90\x80\x80", // 6.7.4
                                 "\xee\x80\x80",     // 6.11.2
                                 "\xef\xbb\xbf"};
  const char *badsequences[] = {
      "\xc3\x28",                                 // 0
      "\xa0\xa1",                                 // 1
      "\xe2\x28\xa1",                             // 2
      "\xe2\x82\x28",                             // 3
      "\xf0\x28\x8c\xbc",                         // 4
      "\xf0\x90\x28\xbc",                         // 5
      "\xf0\x28\x8c\x28",                         // 6
      "\xc0\x9f",                                 // 7
      "\xf5\xff\xff\xff",                         // 8
      "\xed\xa0\x81",                             // 9
      "\xf8\x90\x80\x80\x80",                     // 10
      "123456789012345\xed",                      // 11
      "123456789012345\xf1",                      // 12
      "123456789012345\xc2",                      // 13
      "\xC2\x7F",                                 // 14
      "\xce",                                     // 6.6.1
      "\xce\xba\xe1",                             // 6.6.3
      "\xce\xba\xe1\xbd",                         // 6.6.4
      "\xce\xba\xe1\xbd\xb9\xcf",                 // 6.6.6
      "\xce\xba\xe1\xbd\xb9\xcf\x83\xce",         // 6.6.8
      "\xce\xba\xe1\xbd\xb9\xcf\x83\xce\xbc\xce", // 6.6.10
      "\xdf",                                     // 6.14.6
      "\xef\xbf",                                 // 6.14.7
      "\x80",
      "\x91\x85\x95\x9e",
      "\x6c\x02\x8e\x18"};
  for (size_t i = 0; i < 9; i++) {
    size_t len = strlen(goodsequences[i]);
    if (!simdjson::validate_utf8(goodsequences[i], len)) {
      printf("bug goodsequences[%zu]\n", i);
      abort();
    }
  }
  for (size_t i = 0; i < 26; i++) {
    size_t len = strlen(badsequences[i]);
    if (simdjson::validate_utf8(badsequences[i], len)) {
      printf("bug lookup2 badsequences[%zu]\n", i);
      abort();
    }
  }
  printf("tests ok.\n");
}
int main() {
  brute_force_tests();
  test();
  return EXIT_SUCCESS;
}
