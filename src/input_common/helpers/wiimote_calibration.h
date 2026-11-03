// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace InputCommon::WiimoteProtocol {

class GyroCalibration {
  public:
    void Reset() {
        ready = false;
        ResetWindow();
    }

    bool IsReady() const { return ready; }
    const std::array<float, 3> &Zero() const { return zero; }

    bool Add(const std::array<float, 3> &raw, const std::array<bool, 3> &slow,
             const std::array<float, 3> &accel, std::uint64_t timestamp) {
        if (ready) {
            return false;
        }
        const float gravity = accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2];
        if (!slow[0] || !slow[1] || !slow[2] || gravity < 0.75f * 0.75f ||
            gravity > 1.25f * 1.25f) {
            ResetWindow();
            return false;
        }
        if (count != 0) {
            bool moving = timestamp <= last || timestamp - last > 100000;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                moving = moving || std::abs(raw[axis] - mean[axis]) > 12.0 ||
                         std::abs(accel[axis] - mean[axis + 3]) > 0.08;
            }
            if (moving) {
                ResetWindow();
            }
        }
        if (count == 0) {
            first = timestamp;
        }
        last = timestamp;
        ++count;
        for (std::size_t axis = 0; axis < 6; ++axis) {
            const double value = axis < 3 ? raw[axis] : accel[axis - 3];
            const double difference = value - mean[axis];
            mean[axis] += difference / count;
            squared_error[axis] += difference * (value - mean[axis]);
        }
        if (count < 100 || timestamp - first < 2000000) {
            return false;
        }
        for (std::size_t axis = 0; axis < 6; ++axis) {
            const double variance_limit = axis < 3 ? 9.0 : 0.025 * 0.025;
            if (squared_error[axis] / count > variance_limit) {
                ResetWindow();
                return false;
            }
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            zero[axis] = static_cast<float>(mean[axis]);
        }
        ready = true;
        return true;
    }

  private:
    void ResetWindow() {
        count = 0;
        first = last = 0;
        mean = squared_error = {};
    }
    bool ready{};
    unsigned count{};
    std::uint64_t first{}, last{};
    std::array<double, 6> mean{}, squared_error{};
    std::array<float, 3> zero{8192.0f, 8192.0f, 8192.0f};
};

} // namespace InputCommon::WiimoteProtocol
