#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ledup {

inline uint8_t reverse8(uint8_t value) {
  uint8_t result = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    result = static_cast<uint8_t>((result << 1U) | (value & 1U));
    value >>= 1U;
  }
  return result;
}

inline uint16_t reverse16(uint16_t value) {
  uint16_t result = 0;
  for (uint8_t i = 0; i < 16; ++i) {
    result = static_cast<uint16_t>((result << 1U) | (value & 1U));
    value >>= 1U;
  }
  return result;
}

inline uint16_t crc16(const uint8_t *address, size_t address_length,
                      const uint8_t *data, size_t data_length) {
  uint16_t crc = 0xFFFF;

  for (size_t index = address_length; index > 0; --index) {
    crc ^= static_cast<uint16_t>(address[index - 1]) << 8U;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
    }
  }

  for (size_t index = 0; index < data_length; ++index) {
    crc ^= static_cast<uint16_t>(reverse8(data[index])) << 8U;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
    }
  }

  return static_cast<uint16_t>(~reverse16(crc));
}

inline std::array<uint8_t, 7> whitening_context(uint8_t value) {
  return {1U,
          static_cast<uint8_t>((value >> 5U) & 1U),
          static_cast<uint8_t>((value >> 4U) & 1U),
          static_cast<uint8_t>((value >> 3U) & 1U),
          static_cast<uint8_t>((value >> 2U) & 1U),
          static_cast<uint8_t>((value >> 1U) & 1U),
          static_cast<uint8_t>(value & 1U)};
}

inline void whiten(uint8_t *data, size_t length,
                   std::array<uint8_t, 7> &context) {
  for (size_t index = 0; index < length; ++index) {
    const uint8_t value6 = context[6];
    const uint8_t value5 = context[5];
    const uint8_t value4 = context[4];
    const uint8_t value3 = context[3];
    const uint8_t value52 = value5 ^ context[2];
    const uint8_t value41 = value4 ^ context[1];
    const uint8_t value63 = value6 ^ context[3];
    const uint8_t value630 = value63 ^ context[0];

    context = {static_cast<uint8_t>(value52 ^ value6), value630, value41,
               value52, static_cast<uint8_t>(value52 ^ value3),
               static_cast<uint8_t>(value630 ^ value4),
               static_cast<uint8_t>(value41 ^ value5)};

    const uint8_t value = data[index];
    data[index] = static_cast<uint8_t>(
        ((value & 0x80U) ^ ((value52 ^ value6) << 7U)) |
        ((value & 0x40U) ^ (value630 << 6U)) |
        ((value & 0x20U) ^ (value41 << 5U)) |
        ((value & 0x10U) ^ (value52 << 4U)) |
        ((value & 0x08U) ^ (value63 << 3U)) |
        ((value & 0x04U) ^ (value4 << 2U)) |
        ((value & 0x02U) ^ (value5 << 1U)) |
        ((value & 0x01U) ^ value6));
  }
}

inline std::array<uint8_t, 16>
build_manufacturer_payload(uint8_t packet_id, uint8_t remote_group,
                           uint8_t key_code, uint8_t click_count) {
  constexpr std::array<uint8_t, 3> address{0x5A, 0x00, 0x10};
  const std::array<uint8_t, 8> command{0xFF, 0xFF, 0xFF, packet_id,
                                       remote_group, key_code, 0xFF,
                                       click_count};

  std::array<uint8_t, 31> buffer{};
  buffer[0x0F] = 0x71;
  buffer[0x10] = 0x0F;
  buffer[0x11] = 0x55;

  for (size_t index = 0; index < address.size(); ++index) {
    buffer[0x12 + address.size() - index - 1] = address[index];
  }
  for (size_t index = 0; index < command.size(); ++index) {
    buffer[0x12 + address.size() + index] = command[index];
  }
  for (size_t index = 0; index < address.size() + 3; ++index) {
    buffer[0x0F + index] = reverse8(buffer[0x0F + index]);
  }

  const uint16_t checksum =
      crc16(address.data(), address.size(), command.data(), command.size());
  buffer[0x12 + address.size() + command.size()] =
      static_cast<uint8_t>(checksum & 0xFFU);
  buffer[0x13 + address.size() + command.size()] =
      static_cast<uint8_t>(checksum >> 8U);

  auto context3f = whitening_context(0x3F);
  whiten(buffer.data() + 0x12, address.size() + command.size() + 2,
         context3f);
  auto context25 = whitening_context(0x25);
  whiten(buffer.data(), buffer.size(), context25);

  std::array<uint8_t, 16> payload{};
  for (size_t index = 0; index < payload.size(); ++index) {
    payload[index] = buffer[0x0F + index];
  }
  return payload;
}

}  // namespace ledup
