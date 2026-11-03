// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace InputCommon::WiimoteProtocol {
inline std::array<float, 2> StickFromButtons(std::uint16_t buttons) {
    float x =
        static_cast<float>((buttons & 0x0200) != 0) - static_cast<float>((buttons & 0x0100) != 0);
    float y =
        static_cast<float>((buttons & 0x0800) != 0) - static_cast<float>((buttons & 0x0400) != 0);
    if (x != 0 && y != 0) {
        x *= 0.70710678f;
        y *= 0.70710678f;
    }
    return {x, y};
}

struct Sample {
    std::uint16_t buttons{};
    std::array<float, 3> accel{};
    std::optional<std::array<float, 3>> gyro;
    std::array<float, 3> gyro_raw{};
    std::array<bool, 3> gyro_slow{};
};

inline std::array<float, 3>
GyroRates(const std::array<float, 3> &raw, const std::array<bool, 3> &slow,
          const std::array<float, 3> &zero = {8192.0f, 8192.0f, 8192.0f}) {
    std::array<float, 3> result{};
    constexpr std::array sign{-1.0f, -1.0f, 1.0f};
    for (std::size_t axis = 0; axis < result.size(); ++axis) {
        result[axis] = sign[axis] * (raw[axis] - zero[axis]) * (slow[axis] ? 440.0f : 2000.0f) /
                       (8192.0f * 360.0f);
    }
    return result;
}

inline std::optional<Sample> Decode(std::span<const std::uint8_t> report) {
    if (report.size() < 6 || (report[0] != 0x31 && report[0] != 0x35 && report[0] != 0x37)) {
        return std::nullopt;
    }
    const std::size_t offset = report[0] == 0x37 ? 16 : 6;
    if (report[0] != 0x31 && report.size() < offset + 6) {
        return std::nullopt;
    }
    if ((report[3] == 0xff && report[4] == 0xff && report[5] == 0xff) ||
        (report[3] == 0 && report[4] == 0 && report[5] == 0)) {
        return std::nullopt;
    }
    Sample sample;
    sample.buttons = static_cast<std::uint16_t>((report[1] << 8) | report[2]);
    const auto accel = [](int raw) { return static_cast<float>(raw - 512) / 104.0f; };
    sample.accel = {accel((report[3] << 2) | ((report[1] >> 5) & 3)),
                    accel((report[4] << 2) | ((report[2] >> 4) & 2)),
                    -accel((report[5] << 2) | ((report[2] >> 5) & 2))};
    if (report[0] != 0x31) {
        const auto ext = report.subspan(offset, 6);
        if ((ext[5] & 3) == 2) {
            const auto raw = [](std::uint8_t low, std::uint8_t high) {
                return static_cast<float>(low | ((high & 0xfc) << 6));
            };
            sample.gyro_raw = {raw(ext[2], ext[5]), raw(ext[1], ext[4]), raw(ext[0], ext[3])};
            sample.gyro_slow = {(ext[3] & 1) != 0, (ext[4] & 2) != 0, (ext[3] & 2) != 0};
            sample.gyro = GyroRates(sample.gyro_raw, sample.gyro_slow);
        }
    }
    return sample;
}
} // namespace InputCommon::WiimoteProtocol
