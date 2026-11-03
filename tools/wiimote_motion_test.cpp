// SPDX-License-Identifier: GPL-3.0-or-later
#include "hid_core/frontend/motion_input.h"
#include "input_common/helpers/wiimote_calibration.h"
#include "input_common/helpers/wiimote_protocol.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

void Check(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    using namespace InputCommon::WiimoteProtocol;
    // Representative raw stationary readings from the tested right-hand clone.
    const std::array<float, 3> raw{8434.0f, 7678.0f, 8063.0f};
    const std::array slow{true, true, true};
    const std::array gravity{0.0f, 0.0f, -1.038462f};
    GyroCalibration calibration;
    Core::HID::MotionInput old_filter, corrected_filter;
    old_filter.SetUserGyroThreshold(0.007f);
    corrected_filter.SetUserGyroThreshold(0.0005f);
    for (unsigned frame = 0; frame < 600; ++frame) {
        calibration.Add(raw, slow, gravity, 1000000 + frame * 10000);
        const auto uncorrected = GyroRates(raw, slow);
        const auto corrected = calibration.IsReady() ? GyroRates(raw, slow, calibration.Zero())
                                                     : std::array<float, 3>{};
        old_filter.SetAcceleration({gravity[0], gravity[1], gravity[2]});
        corrected_filter.SetAcceleration({gravity[0], gravity[1], gravity[2]});
        old_filter.SetGyroscope({uncorrected[0], uncorrected[1], uncorrected[2]});
        corrected_filter.SetGyroscope({corrected[0], corrected[1], corrected[2]});
        old_filter.UpdateRotation(10000);
        old_filter.UpdateOrientation(10000);
        corrected_filter.UpdateRotation(10000);
        corrected_filter.UpdateOrientation(10000);
    }
    Check(old_filter.IsMoving(Core::HID::MotionInput::IsAtRestRelaxed),
          "regression reproduces false motion in old nominal calibration");
    Check(!corrected_filter.IsMoving(Core::HID::MotionInput::IsAtRestTight),
          "calibrated remote is stationary through the actual emulator filter");
    Check(corrected_filter.GetRotations().Length() < 0.0001f,
          "stationary calibration does not accumulate false rotation");
    auto moving = raw;
    moving[0] -= 10;
    const auto fine_rotation = GyroRates(moving, slow, calibration.Zero());
    corrected_filter.SetGyroscope({fine_rotation[0], fine_rotation[1], fine_rotation[2]});
    Check(corrected_filter.GetGyroscope()[0] > 0.001f,
          "fine sub-degree-per-second aiming survives the actual motion filter");
    std::puts("PASS: false-motion regression, stationary orientation, and fine aiming filter.");
}
