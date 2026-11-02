#include "../ledup_protocol.h"

#include <array>
#include <cassert>
#include <cstdint>

int main() {
  const std::array<uint8_t, 16> first_expected{
      0x6D, 0xB6, 0x43, 0x64, 0x5D, 0x16, 0xFB, 0x6D,
      0x1A, 0x1C, 0xF6, 0xAE, 0xAE, 0xFA, 0x25, 0x39};
  assert(ledup::build_manufacturer_payload(1, 8, 22, 0) == first_expected);

  // Captured from the published nctr010f LEDUP Android transmission.
  const std::array<uint8_t, 16> captured_expected{
      0x6D, 0xB6, 0x43, 0x64, 0x5D, 0x16, 0xFB, 0x6D,
      0x1A, 0xA9, 0xF6, 0xAE, 0xAE, 0xA7, 0x95, 0xC8};
  assert(ledup::build_manufacturer_payload(180, 8, 22, 93) == captured_expected);

  return 0;
}
