// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <span>
#include <thread>
#include <SDL3/SDL_hidapi.h>
#include "input_common/input_engine.h"

namespace InputCommon {

class Wiimotes final : public InputEngine {
public:
    explicit Wiimotes(const std::string& engine);
    ~Wiimotes() override;
    std::vector<Common::ParamPackage> GetInputDevices() const override;
    ButtonMapping GetButtonMappingForDevice(const Common::ParamPackage& params) override;
    AnalogMapping GetAnalogMappingForDevice(const Common::ParamPackage& params) override;
    MotionMapping GetMotionMappingForDevice(const Common::ParamPackage& params) override;
    bool IsVibrationEnabled(const PadIdentifier& identifier) override;
    Common::Input::DriverResult SetVibration(
        const PadIdentifier& identifier, const Common::Input::VibrationStatus& vibration) override;

private:
    struct Remote {
        SDL_hid_device* handle{};
        std::atomic<bool> connected{};
        std::atomic<bool> rumble{};
        bool last_rumble{};
        BasicMotion motion{};
        u64 last_sample{};
        u64 last_input{};
        std::atomic<bool> has_gyro{};
    };
    PadIdentifier Identifier(std::size_t port) const;
    Common::ParamPackage Params(std::size_t port) const;
    void Run(std::stop_token stop);
    void Scan(std::stop_token stop);
    void Initialize(std::size_t port, std::stop_token stop);
    void Process(std::size_t port, std::span<const u8> report);
    void Disconnect(std::size_t port);
    bool Command(Remote& remote, std::span<const u8> report);
    bool WriteRegister(Remote& remote, u8 device, u8 address, u8 value, std::stop_token stop);
    std::array<Remote, 2> remotes{};
    std::jthread worker;
};

} // namespace InputCommon
