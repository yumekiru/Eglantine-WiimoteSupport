// SPDX-License-Identifier: GPL-3.0-or-later
#include "input_common/helpers/wiimote_protocol.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

using InputCommon::WiimoteProtocol::Decode;
void Check(bool value, const char *label) {
  if (!value) {
    std::fprintf(stderr, "FAIL: %s\n", label);
    std::exit(1);
  }
}
void Near(float actual, float expected, const char *label) {
  Check(std::abs(actual - expected) < 0.0001f, label);
}
int main() {
  using InputCommon::WiimoteProtocol::StickFromButtons;
  Check(StickFromButtons(0) == std::array{0.0f, 0.0f},
        "released D-pad centers stick");
  Check(StickFromButtons(0x0800) == std::array{0.0f, 1.0f},
        "D-pad up is positive Y");
  Check(StickFromButtons(0x0400) == std::array{0.0f, -1.0f},
        "D-pad down is negative Y");
  Check(StickFromButtons(0x0100) == std::array{-1.0f, 0.0f},
        "D-pad left is negative X");
  Check(StickFromButtons(0x0200) == std::array{1.0f, 0.0f},
        "D-pad right is positive X");
  Check(StickFromButtons(0x0f00) == std::array{0.0f, 0.0f},
        "opposite D-pad directions cancel");
  const auto diagonal = StickFromButtons(0x0a00);
  Near(diagonal[0] * diagonal[0] + diagonal[1] * diagonal[1], 1,
       "diagonal stays in unit circle");
  std::array<std::uint8_t, 22> packet{0x35, 0, 8, 0x80, 0x80, 0x9a,
                                      0,    0, 0, 0x83, 0x82, 0x82};
  auto sample = Decode(packet);
  Check(sample.has_value() && sample->gyro.has_value(),
        "valid stationary MotionPlus frame");
  Check((sample->buttons & 8) != 0, "A button");
  Near(sample->accel[0], 0, "gravity X");
  Near(sample->accel[1], 0, "gravity Y");
  Near(sample->accel[2], -1, "gravity Z and handedness");
  for (auto rate : *sample->gyro)
    Near(rate, 0, "stationary gyro");
  packet[6] = 0;
  packet[9] = 0x93;
  sample = Decode(packet);
  Near((*sample->gyro)[2], 1024.0f * 440.0f / (8192.0f * 360.0f),
       "slow yaw units");
  packet[9] = 0x91;
  sample = Decode(packet);
  Near((*sample->gyro)[2], 1024.0f * 2000.0f / (8192.0f * 360.0f),
       "fast yaw scale");
  packet[9] = 0x83;
  packet[10] = 0x92;
  sample = Decode(packet);
  Near((*sample->gyro)[1], -1024.0f * 440.0f / (8192.0f * 360.0f), "roll sign");
  packet[10] = 0x82;
  packet[11] = 0x92;
  sample = Decode(packet);
  Near((*sample->gyro)[0], -1024.0f * 440.0f / (8192.0f * 360.0f),
       "pitch sign");
  std::array<std::uint8_t, 22> ir{0x37, 0, 8, 0x80, 0x80, 0x9a};
  for (std::size_t i = 0; i < 6; ++i)
    ir[16 + i] = packet[6 + i];
  Check(Decode(ir)->gyro == sample->gyro, "IR report extension offset");
  Check(!Decode(std::span(ir.data(), 21)), "truncated IR report rejected");
  packet[11] = 0;
  Check(!Decode(packet)->gyro, "non-gyro extension rejected");
  packet[0] = 0x31;
  Check(Decode(std::span(packet.data(), 6)).has_value(), "accel-only report");
  packet[0] = 0x21;
  Check(!Decode(packet), "memory/error report rejected");
  packet[0] = 0x35;
  packet[3] = packet[4] = packet[5] = 0xff;
  Check(!Decode(packet), "invalid all-FF accelerometer rejected");
  Check(!Decode({}), "empty report rejected");
  std::puts("PASS: gravity, buttons, gyro units/signs, slow/fast scaling, "
            "offsets, invalid packets.");
}
