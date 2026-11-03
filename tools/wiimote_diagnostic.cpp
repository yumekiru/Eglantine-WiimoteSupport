// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/logging.h"
#include "input_common/drivers/wiimote.h"
#include <SDL3/SDL.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

int main() {
  Common::Log::Initialize();
  Common::Log::Start();
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_WII, "0");
  std::puts("Direct DolphinBar test: close Dolphin, connect both remotes, "
            "gently rotate each.\n"
            "LED 1 = left hand; LED 2 = right hand. Waiting 20 seconds.");
  std::fflush(stdout);
  std::array<std::atomic<unsigned>, 2> counts{};
  InputCommon::Wiimotes driver("wiimote");
  for (std::size_t port = 0; port < counts.size(); ++port) {
    driver.SetCallback(
        {.identifier = {.guid = Common::UUID{}, .port = port, .pad = 0},
         .type = EngineInputType::Motion,
         .index = 0,
         .callback = {[&counts, port] { ++counts[port]; }}});
  }
  std::this_thread::sleep_for(std::chrono::seconds(20));
  bool success = true;
  const auto devices = driver.GetInputDevices();
  for (std::size_t port = 0; port < counts.size(); ++port) {
    bool gyro = false;
    for (const auto &device : devices) {
      if (!device.Has("guid2") &&
          device.Get("port", -1) == static_cast<int>(port)) {
        gyro = device.Get("has_gyro", 0) != 0;
      }
    }
    const auto motion =
        driver.GetMotion({.guid = Common::UUID{}, .port = port, .pad = 0}, 0);
    std::printf(
        "Remote %zu: samples=%u MotionPlus=%s accel(g)=(%.3f, %.3f, %.3f) "
        "gyro(turns/s)=(%.3f, %.3f, %.3f)\n",
        port + 1, counts[port].load(), gyro ? "YES" : "NO", motion.accel_x,
        motion.accel_y, motion.accel_z, motion.gyro_x, motion.gyro_y,
        motion.gyro_z);
    success = success && gyro && counts[port] > 10;
  }
  std::puts(success ? "PASS: both independent MotionPlus streams received."
                    : "INCOMPLETE: both independent MotionPlus streams were "
                      "not verified.");
  return success ? 0 : 2;
}
