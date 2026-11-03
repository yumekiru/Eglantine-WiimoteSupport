// SPDX-License-Identifier: GPL-3.0-or-later
#include "input_common/drivers/wiimote.h"
#include "input_common/helpers/wiimote_protocol.h"

#include "common/logging.h"
#include "common/settings.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace InputCommon {
namespace {
u64 Now() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}
constexpr std::array<u16, 11> ButtonMasks{
    0x0008, 0x0004, 0x0002, 0x0001, 0x1000, 0x0010, 0x0080, 0x0800, 0x0400, 0x0100, 0x0200,
};
} // namespace

Wiimotes::Wiimotes(const std::string &engine) : InputEngine(engine) {
    for (std::size_t port = 0; port < remotes.size(); ++port) {
        PreSetController(Identifier(port));
        for (int button = 0; button < static_cast<int>(ButtonMasks.size()); ++button) {
            PreSetButton(Identifier(port), button);
        }
        PreSetMotion(Identifier(port), 0);
        PreSetAxis(Identifier(port), 0);
        PreSetAxis(Identifier(port), 1);
    }
    if (SDL_hid_init() == 0) {
        worker = std::jthread([this](std::stop_token stop) { Run(stop); });
    }
}

Wiimotes::~Wiimotes() {
    worker = {};
    for (std::size_t port = 0; port < remotes.size(); ++port) {
        Disconnect(port);
    }
    SDL_hid_exit();
}

PadIdentifier Wiimotes::Identifier(std::size_t port) const {
    return {.guid = Common::UUID{}, .port = port, .pad = 0};
}

Common::ParamPackage Wiimotes::Params(std::size_t port) const {
    return {{"engine", GetEngineName()},
            {"guid", Identifier(port).guid.RawString()},
            {"port", std::to_string(port)},
            {"pad", "0"}};
}

bool Wiimotes::Command(Remote &remote, std::span<const u8> report) {
    std::array<u8, 22> padded{};
    std::ranges::copy(report, padded.begin());
    padded[1] = static_cast<u8>((padded[1] & 0xfe) | (remote.rumble.load() ? 1 : 0));
    return SDL_hid_write(remote.handle, padded.data(), padded.size()) >= 0;
}

bool Wiimotes::WriteRegister(Remote &remote, u8 device, u8 address, u8 value,
                             std::stop_token stop) {
    const std::array<u8, 7> output{0x16, 0x06, device, 0, address, 1, value};
    if (!Command(remote, output)) {
        return false;
    }
    const auto deadline = Now() + 1000000;
    std::array<u8, 64> input{};
    while (!stop.stop_requested() && Now() < deadline) {
        const int size = SDL_hid_read_timeout(remote.handle, input.data(), input.size(), 10);
        if (size >= 5 && input[0] == 0x22 && input[3] == 0x16) {
            return input[4] == 0;
        }
        if (size < 0) {
            break;
        }
    }
    return false;
}

void Wiimotes::Initialize(std::size_t port, std::stop_token stop) {
    auto &remote = remotes[port];
    const std::array<u8, 2> leds{0x11, static_cast<u8>(0x10u << port)};
    Command(remote, leds);
    // DolphinBar drops memory-read replies from some Motion 2-in-1 remotes.
    // Reset and activate the peripheral using writes, then validate actual gyro packets.
    WriteRegister(remote, 0xa4, 0xf0, 0x55, stop);
    WriteRegister(remote, 0xa4, 0xfb, 0, stop);
    SDL_Delay(50);
    WriteRegister(remote, 0xa6, 0xf0, 0x55, stop);
    WriteRegister(remote, 0xa6, 0xfe, 4, stop);
    SDL_Delay(300);
    const std::array<u8, 3> mode{0x12, 0x06, 0x35};
    Command(remote, mode);
    remote.last_input = Now();
    LOG_INFO(Input, "DolphinBar slot {} initialized without memory reads", port + 1);
}

void Wiimotes::Scan(std::stop_token stop) {
    auto *devices = SDL_hid_enumerate(0x057e, 0x0306);
    for (auto *device = devices; device && !stop.stop_requested(); device = device->next) {
        if (!device->product_string ||
            std::wstring(device->product_string).find(L"Mayflash") == std::wstring::npos ||
            device->interface_number < 0 ||
            device->interface_number >= static_cast<int>(remotes.size())) {
            continue;
        }
        const auto port = static_cast<std::size_t>(device->interface_number);
        auto &remote = remotes[port];
        if (remote.handle) {
            continue;
        }
        remote.handle = SDL_hid_open_path(device->path);
        if (!remote.handle) {
            continue;
        }
        const std::array<u8, 2> status{0x15, 0};
        if (!Command(remote, status)) {
            Disconnect(port);
            continue;
        }
        Initialize(port, stop);
    }
    SDL_hid_free_enumeration(devices);
}

void Wiimotes::Disconnect(std::size_t port) {
    auto &remote = remotes[port];
    remote.connected = false;
    if (remote.handle) {
        const std::array<u8, 2> off{0x10, 0};
        remote.rumble = false;
        Command(remote, off);
        SDL_hid_close(remote.handle);
        remote.handle = nullptr;
    }
    remote.motion = {};
    remote.last_sample = 0;
    remote.has_gyro = false;
    remote.calibrated = false;
    remote.calibration.Reset();
    remote.last_led_update = 0;
    remote.last_rumble = false;
    SetAxis(Identifier(port), 0, 0);
    SetAxis(Identifier(port), 1, 0);
    for (int button = 0; button < static_cast<int>(ButtonMasks.size()); ++button) {
        SetButton(Identifier(port), button, false);
    }
    SetMotion(Identifier(port), 0, {});
}

void Wiimotes::Process(std::size_t port, std::span<const u8> report) {
    auto &remote = remotes[port];
    const auto sample = WiimoteProtocol::Decode(report);
    if (!sample) {
        return;
    }
    remote.last_input = Now();
    remote.connected = true;
    const auto stick = WiimoteProtocol::StickFromButtons(sample->buttons);
    SetAxis(Identifier(port), 0, stick[0]);
    SetAxis(Identifier(port), 1, stick[1]);
    for (std::size_t button = 0; button < ButtonMasks.size(); ++button) {
        SetButton(Identifier(port), static_cast<int>(button),
                  (sample->buttons & ButtonMasks[button]) != 0);
    }
    remote.motion.accel_x = sample->accel[0];
    remote.motion.accel_y = sample->accel[1];
    remote.motion.accel_z = sample->accel[2];
    if (sample->gyro) {
        if (remote.calibration.Add(sample->gyro_raw, sample->gyro_slow, sample->accel, Now())) {
            remote.calibrated = true;
            const auto &zero = remote.calibration.Zero();
            LOG_INFO(Input, "DolphinBar slot {} gyro zero calibrated: pitch={} roll={} yaw={}",
                     port + 1, zero[0], zero[1], zero[2]);
            const std::array<u8, 2> leds{0x11, static_cast<u8>(0x10u << port)};
            Command(remote, leds);
        }
        const auto gyro = remote.calibration.IsReady()
                              ? WiimoteProtocol::GyroRates(sample->gyro_raw, sample->gyro_slow,
                                                           remote.calibration.Zero())
                              : std::array<float, 3>{};
        remote.motion.gyro_x = gyro[0];
        remote.motion.gyro_y = gyro[1];
        remote.motion.gyro_z = gyro[2];
        if (!remote.has_gyro) {
            LOG_INFO(Input, "DolphinBar slot {} is streaming MotionPlus", port + 1);
            remote.has_gyro = true;
        }
    } else if (remote.has_gyro || report[0] != 0x31) {
        return;
    }
    const auto now = Now();
    if (remote.last_sample != 0) {
        remote.motion.delta_timestamp = std::clamp<u64>(now - remote.last_sample, 1, 100000);
        SetMotion(Identifier(port), 0, remote.motion);
    }
    remote.last_sample = now;
}

void Wiimotes::Run(std::stop_token stop) {
    u64 next_scan = 0;
    std::array<u8, 64> report{};
    while (!stop.stop_requested()) {
        if (Now() >= next_scan) {
            Scan(stop);
            next_scan = Now() + 5000000;
        }
        for (std::size_t port = 0; port < remotes.size(); ++port) {
            auto &remote = remotes[port];
            if (!remote.handle) {
                continue;
            }
            const int size = SDL_hid_read_timeout(remote.handle, report.data(), report.size(), 5);
            if (size < 0 || Now() - remote.last_input > 10000000) {
                Disconnect(port);
                continue;
            }
            if (size > 0) {
                Process(port, std::span(report.data(), static_cast<std::size_t>(size)));
            }
            const bool rumble = remote.rumble.load();
            if (remote.has_gyro && !remote.calibrated && Now() - remote.last_led_update > 250000) {
                const auto mask = static_cast<u8>(((Now() / 250000) & 1) ? (0x10u << port) : 0);
                const std::array<u8, 2> leds{0x11, mask};
                Command(remote, leds);
                remote.last_led_update = Now();
            }
            if (rumble != remote.last_rumble) {
                const std::array<u8, 2> output{0x10, 0};
                Command(remote, output);
                remote.last_rumble = rumble;
            }
        }
        SDL_Delay(1);
    }
}

std::vector<Common::ParamPackage> Wiimotes::GetInputDevices() const {
    std::vector<Common::ParamPackage> devices;
    for (std::size_t port = 0; port < remotes.size(); ++port) {
        if (!remotes[port].connected) {
            continue;
        }
        auto params = Params(port);
        params.Set("has_gyro", remotes[port].has_gyro.load() ? 1 : 0);
        params.Set("calibrated", remotes[port].calibrated.load() ? 1 : 0);
        params.Set("display", port == 0 ? "Wii Remote 1 (Left)" : "Wii Remote 2 (Right)");
        devices.push_back(std::move(params));
    }
    if (remotes[0].connected && remotes[1].connected) {
        auto params = Params(1);
        params.Set("display", "Wii Remote Pair 1 (Left + Right)");
        params.Set("guid2", Identifier(0).guid.RawString());
        params.Set("port2", 0);
        devices.push_back(std::move(params));
    }
    return devices;
}

ButtonMapping Wiimotes::GetButtonMappingForDevice(const Common::ParamPackage &params) {
    const auto right = static_cast<std::size_t>(params.Get("port", 0));
    const auto left = static_cast<std::size_t>(params.Get("port2", params.Get("port", 0)));
    if (left >= remotes.size() || right >= remotes.size()) {
        return {};
    }
    ButtonMapping mapping;
    for (int button = 0; button < Settings::NativeButton::NumButtons; ++button) {
        mapping.insert_or_assign(static_cast<Settings::NativeButton::Values>(button),
                                 Common::ParamPackage{{"engine", "keyboard"}, {"code", "0"}});
    }
    const auto bind = [&](Settings::NativeButton::Values button, std::size_t port, int index) {
        auto source = Params(port);
        source.Set("button", index);
        mapping.insert_or_assign(button, std::move(source));
    };
    bind(Settings::NativeButton::A, right, 0);
    bind(Settings::NativeButton::B, right, 1);
    bind(Settings::NativeButton::X, right, 4);
    bind(Settings::NativeButton::Y, right, 5);
    bind(Settings::NativeButton::Plus, right, 6);
    bind(Settings::NativeButton::L, left, 2);
    bind(Settings::NativeButton::R, right, 2);
    bind(Settings::NativeButton::ZL, left, 3);
    bind(Settings::NativeButton::ZR, right, 3);
    bind(Settings::NativeButton::SLLeft, left, 2);
    bind(Settings::NativeButton::SRLeft, left, 3);
    bind(Settings::NativeButton::SLRight, right, 2);
    bind(Settings::NativeButton::SRRight, right, 3);
    return mapping;
}

AnalogMapping Wiimotes::GetAnalogMappingForDevice(const Common::ParamPackage &params) {
    const auto right = static_cast<std::size_t>(params.Get("port", 0));
    const auto left = static_cast<std::size_t>(params.Get("port2", params.Get("port", 0)));
    if (left >= remotes.size() || right >= remotes.size()) {
        return {};
    }
    auto left_stick = Params(left);
    auto right_stick = Params(right);
    for (auto *stick : {&left_stick, &right_stick}) {
        stick->Set("axis_x", 0);
        stick->Set("axis_y", 1);
    }
    return {{Settings::NativeAnalog::LStick, std::move(left_stick)},
            {Settings::NativeAnalog::RStick, std::move(right_stick)}};
}

MotionMapping Wiimotes::GetMotionMappingForDevice(const Common::ParamPackage &params) {
    auto left = Params(static_cast<std::size_t>(params.Get("port2", params.Get("port", 0))));
    auto right = Params(static_cast<std::size_t>(params.Get("port", 0)));
    left.Set("motion", 0);
    right.Set("motion", 0);
    return {{Settings::NativeMotion::MotionLeft, std::move(left)},
            {Settings::NativeMotion::MotionRight, std::move(right)}};
}

bool Wiimotes::IsVibrationEnabled(const PadIdentifier &identifier) {
    return identifier.port < remotes.size() && remotes[identifier.port].connected;
}

Common::Input::DriverResult
Wiimotes::SetVibration(const PadIdentifier &identifier,
                       const Common::Input::VibrationStatus &vibration) {
    if (!IsVibrationEnabled(identifier)) {
        return Common::Input::DriverResult::NotSupported;
    }
    remotes[identifier.port].rumble = vibration.low_amplitude > 0 || vibration.high_amplitude > 0;
    return Common::Input::DriverResult::Success;
}

} // namespace InputCommon
