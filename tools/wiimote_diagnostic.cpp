// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/logging.h"
#include "input_common/drivers/wiimote.h"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>

struct StationaryCapture {
    std::mutex mutex;
    std::array<unsigned, 2> count{};
    std::array<std::array<double, 3>, 2> mean{}, squared_error{};
    void Add(std::size_t port, const BasicMotion &motion) {
        std::scoped_lock lock{mutex};
        ++count[port];
        const std::array rates{motion.gyro_x * 360.0, motion.gyro_y * 360.0, motion.gyro_z * 360.0};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double difference = rates[axis] - mean[port][axis];
            mean[port][axis] += difference / count[port];
            squared_error[port][axis] += difference * (rates[axis] - mean[port][axis]);
        }
    }
};

int main(int argc, char **argv) {
    const bool stationary = argc > 1 && std::strcmp(argv[1], "--stationary") == 0;
    Common::Log::Initialize();
    Common::Log::Start();
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, "0");
    std::puts(stationary
                  ? "Keep both remotes completely still for this 20-second calibration/drift test."
                  : "Keep both remotes still until LEDs stop flashing, then gently rotate each. "
                    "20-second test.");
    std::fflush(stdout);
    std::array<std::atomic<unsigned>, 2> counts{};
    StationaryCapture capture;
    const auto start = std::chrono::steady_clock::now();
    InputCommon::Wiimotes driver("wiimote");
    for (std::size_t port = 0; port < counts.size(); ++port) {
        driver.SetCallback({.identifier = {.guid = Common::UUID{}, .port = port, .pad = 0},
                            .type = EngineInputType::Motion,
                            .index = 0,
                            .callback = {[&driver, &counts, &capture, start, stationary, port] {
                                ++counts[port];
                                if (stationary && std::chrono::steady_clock::now() - start >
                                                      std::chrono::seconds(5)) {
                                    const auto motion = driver.GetMotion(
                                        {.guid = Common::UUID{}, .port = port, .pad = 0}, 0);
                                    if (motion.delta_timestamp != 0)
                                        capture.Add(port, motion);
                                }
                            }}});
    }
    std::this_thread::sleep_for(std::chrono::seconds(20));
    bool success = true;
    const auto devices = driver.GetInputDevices();
    std::scoped_lock lock{capture.mutex};
    for (std::size_t port = 0; port < counts.size(); ++port) {
        bool gyro = false, calibrated = false;
        for (const auto &device : devices) {
            if (!device.Has("guid2") && device.Get("port", -1) == static_cast<int>(port)) {
                gyro = device.Get("has_gyro", 0) != 0;
                calibrated = device.Get("calibrated", 0) != 0;
            }
        }
        const auto motion = driver.GetMotion({.guid = Common::UUID{}, .port = port, .pad = 0}, 0);
        std::printf(
            "Remote %zu: samples=%u MotionPlus=%s calibrated=%s gyro(dps)=(%.5f, %.5f, %.5f)\n",
            port + 1, counts[port].load(), gyro ? "YES" : "NO", calibrated ? "YES" : "NO",
            motion.gyro_x * 360, motion.gyro_y * 360, motion.gyro_z * 360);
        success = success && gyro && calibrated && counts[port] > 10;
        if (stationary) {
            std::array<double, 3> deviation{};
            for (std::size_t axis = 0; axis < 3; ++axis) {
                deviation[axis] = std::sqrt(capture.squared_error[port][axis] /
                                            std::max(1u, capture.count[port]));
                success =
                    success && std::abs(capture.mean[port][axis]) < 0.25 && deviation[axis] < 0.25;
            }
            success = success && capture.count[port] > 100;
            std::printf(
                "Remote %zu: rest N=%u mean_dps=(%.6f, %.6f, %.6f) std_dps=(%.6f, %.6f, %.6f)\n",
                port + 1, capture.count[port], capture.mean[port][0], capture.mean[port][1],
                capture.mean[port][2], deviation[0], deviation[1], deviation[2]);
        }
    }
    std::puts(success ? "PASS: both calibrated motion streams verified."
                      : "INCOMPLETE: motion/calibration checks failed.");
    return success ? 0 : 2;
}
