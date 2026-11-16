// Force feedback on Windows: DirectInput 8, which every force feedback
// wheel's driver serves (Logitech G HUB, Thrustmaster, Fanatec, Moza). A
// constant force, a damper and friction (where the wheel's driver has it),
// played for as long as the wheel's held. The wheel's rotation stays the
// player's, set in its maker's software. Godot raises the system timer's
// resolution (timeBeginPeriod), so the thread's short waits hold.

#include "wheel_log.h"
#include "known_wheels.h"
#include "wheel_output.h"

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// DirectInput's forces run from -DI_FFNOMINALMAX to DI_FFNOMINALMAX.
constexpr LONG kNominal = DI_FFNOMINALMAX;

std::string utf8(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size - 1 : 0, '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    }
    return out;
}

BOOL CALLBACK take_each(LPCDIDEVICEINSTANCEW instance, LPVOID found) {
    static_cast<std::vector<DIDEVICEINSTANCEW>*>(found)->push_back(*instance);
    return DIENUM_CONTINUE;
}

// A DirectInput device's USB IDs: its product GUID's first field is
// MAKELONG(vendor, product).
std::uint16_t vendor_of(const DIDEVICEINSTANCEW& wheel) {
    return LOWORD(wheel.guidProduct.Data1);
}

std::uint16_t product_of(const DIDEVICEINSTANCEW& wheel) {
    return HIWORD(wheel.guidProduct.Data1);
}

FoundWheel found_wheel(const DIDEVICEINSTANCEW& wheel) {
    // Every wheel's maker serves DirectInput, so none is new here. Its family
    // is the wheel's own where it's known (a G920's is "hidpp", as on a Mac):
    // what its motor needs follows the wheel, not the API, and as
    // "directinput" a geared wheel lost its least force and never centred.
    const wheel_registry::KnownWheel* known = wheel_registry::known_wheel(vendor_of(wheel), product_of(wheel));
    return FoundWheel{utf8(wheel.tszProductName), true, vendor_of(wheel), product_of(wheel),
                      known ? wheel_registry::family_id(known->family) : "directinput"};
}

class WindowsWheelOutput : public WheelOutput {
public:
    ~WindowsWheelOutput() override {
        close();
        if (input_) {
            input_->Release();
        }
    }

    std::optional<FoundWheel> find(std::uint16_t vendor, std::uint16_t product) override {
        DIDEVICEINSTANCEW wheel{};
        if (!pick_wheel(vendor, product, wheel)) {
            return std::nullopt;
        }
        return found_wheel(wheel);
    }

    bool open(std::uintptr_t window, std::uint16_t vendor, std::uint16_t product) override {
        close();
        DIDEVICEINSTANCEW wheel{};
        if (!pick_wheel(vendor, product, wheel) || FAILED(input_->CreateDevice(wheel.guidInstance, &device_, nullptr))) {
            return false;
        }
        // Force feedback needs the wheel taken exclusively, for the game's window.
        DIPROPDWORD autocenter{};
        autocenter.diph.dwSize = sizeof(DIPROPDWORD);
        autocenter.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        autocenter.diph.dwHow = DIPH_DEVICE;
        autocenter.dwData = DIPROPAUTOCENTER_OFF;
        if (FAILED(device_->SetDataFormat(&c_dfDIJoystick2)) ||
            FAILED(device_->SetCooperativeLevel(reinterpret_cast<HWND>(window), DISCL_EXCLUSIVE | DISCL_BACKGROUND)) ||
            FAILED(device_->SetProperty(DIPROP_AUTOCENTER, &autocenter.diph)) || FAILED(device_->Acquire()) ||
            !create_effects()) {
            close();
            return false;
        }
        wheel_ = found_wheel(wheel);
        // The steering's range, for reading where it is (position()).
        DIPROPRANGE range{};
        range.diph.dwSize = sizeof(DIPROPRANGE);
        range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        range.diph.dwObj = DIJOFS_X;
        range.diph.dwHow = DIPH_BYOFFSET;
        reads_x_ = SUCCEEDED(device_->GetProperty(DIPROP_RANGE, &range.diph)) && range.lMax > range.lMin;
        x_min_ = range.lMin;
        x_max_ = range.lMax;
        sent_force_ = sent_damper_ = sent_friction_ = 0;
        reports_ = 0;
        return true;
    }

    void close() override {
        if (!device_) {
            return;
        }
        stop();
        for (IDirectInputEffect** effect : {&constant_, &damper_, &friction_}) {
            if (*effect) {
                (*effect)->Unload();
                (*effect)->Release();
                *effect = nullptr;
            }
        }
        device_->Unacquire();
        device_->Release();
        device_ = nullptr;
        wheel_.reset();
    }

    std::optional<FoundWheel> wheel() const override { return wheel_; }

    std::optional<double> position() override {
        if (!device_ || !reads_x_) {
            return std::nullopt;
        }
        device_->Poll();
        DIJOYSTATE2 state{};
        if (FAILED(device_->GetDeviceState(sizeof(state), &state))) {
            return std::nullopt;
        }
        return std::clamp((double(state.lX) - double(x_min_)) / (double(x_max_) - double(x_min_)) * 2.0 - 1.0, -1.0, 1.0);
    }

    bool set_force(double level) override {
        const LONG value = nominal(std::clamp(level, -1.0, 1.0));
        if (value == sent_force_) {
            return true;
        }
        sent_force_ = value;
        DICONSTANTFORCE force{value};
        DIEFFECT effect = describe();
        effect.cbTypeSpecificParams = sizeof(force);
        effect.lpvTypeSpecificParams = &force;
        return update(constant_, effect);
    }

    bool set_damper(double level) override { return set_condition(damper_, sent_damper_, level); }

    // A wheel whose driver has no friction effect plays none.
    bool set_friction(double level) override { return !friction_ || set_condition(friction_, sent_friction_, level); }

    OutputCounts counts() const override { return {reports_, 0}; }

    void stop() override {
        if (device_) {
            set_force(0.0);
            set_damper(0.0);
            set_friction(0.0);
        }
    }

private:
    static LONG nominal(double share) { return static_cast<LONG>(std::lround(share * kNominal)); }

    // The force feedback wheel with these IDs if there's one (and they
    // aren't 0), else the first DirectInput lists.
    bool pick_wheel(std::uint16_t vendor, std::uint16_t product, DIDEVICEINSTANCEW& wheel) {
        if (!input_ && FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                                 reinterpret_cast<void**>(&input_), nullptr))) {
            input_ = nullptr;
            return false;
        }
        std::vector<DIDEVICEINSTANCEW> wheels;
        input_->EnumDevices(DI8DEVCLASS_GAMECTRL, take_each, &wheels, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
        if (wheels.empty()) {
            return false;
        }
        wheel = wheels.front();
        if (vendor != 0 || product != 0) {
            for (const DIDEVICEINSTANCEW& each : wheels) {
                if (vendor_of(each) == vendor && product_of(each) == product) {
                    wheel = each;
                    break;
                }
            }
        }
        return true;
    }

    // One axis (the wheel's), played until stopped.
    DIEFFECT describe() {
        DIEFFECT effect{};
        effect.dwSize = sizeof(DIEFFECT);
        effect.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        effect.dwDuration = INFINITE;
        effect.dwGain = kNominal;
        effect.dwTriggerButton = DIEB_NOTRIGGER;
        effect.cAxes = 1;
        effect.rgdwAxes = axes_;
        effect.rglDirection = direction_;
        return effect;
    }

    bool create_effects() {
        DICONSTANTFORCE force{0};
        DIEFFECT constant = describe();
        constant.cbTypeSpecificParams = sizeof(force);
        constant.lpvTypeSpecificParams = &force;
        DICONDITION condition{};
        DIEFFECT resistance = describe();
        resistance.cbTypeSpecificParams = sizeof(condition);
        resistance.lpvTypeSpecificParams = &condition;
        if (FAILED(device_->CreateEffect(GUID_ConstantForce, &constant, &constant_, nullptr)) ||
            FAILED(device_->CreateEffect(GUID_Damper, &resistance, &damper_, nullptr)) ||
            FAILED(constant_->Start(1, 0)) || FAILED(damper_->Start(1, 0))) {
            return false;
        }
        if (FAILED(device_->CreateEffect(GUID_Friction, &resistance, &friction_, nullptr)) ||
            FAILED(friction_->Start(1, 0))) {
            if (friction_) {
                friction_->Release();
                friction_ = nullptr;
            }
            wheel_log("this wheel's driver has no friction effect; it plays none");
        }
        return true;
    }

    bool set_condition(IDirectInputEffect* effect, LONG& sent, double level) {
        const LONG value = nominal(std::clamp(level, 0.0, 1.0));
        if (value == sent) {
            return true;
        }
        sent = value;
        DICONDITION condition{};
        condition.lPositiveCoefficient = condition.lNegativeCoefficient = value;
        condition.dwPositiveSaturation = condition.dwNegativeSaturation = kNominal;
        DIEFFECT parameters = describe();
        parameters.cbTypeSpecificParams = sizeof(condition);
        parameters.lpvTypeSpecificParams = &condition;
        return update(effect, parameters);
    }

    bool update(IDirectInputEffect* effect, DIEFFECT& parameters) {
        if (!effect) {
            return false;
        }
        ++reports_;
        HRESULT result = effect->SetParameters(&parameters, DIEP_TYPESPECIFICPARAMS);
        if (result == DIERR_INPUTLOST || result == DIERR_NOTACQUIRED) {
            // Taken away (another window, a driver reset): take it back once.
            if (FAILED(device_->Acquire())) {
                return false;
            }
            result = effect->SetParameters(&parameters, DIEP_TYPESPECIFICPARAMS | DIEP_START);
        }
        return SUCCEEDED(result);
    }

    IDirectInput8W* input_ = nullptr;
    IDirectInputDevice8W* device_ = nullptr;
    bool reads_x_ = false;
    LONG x_min_ = 0;
    LONG x_max_ = 0;
    IDirectInputEffect* constant_ = nullptr;
    IDirectInputEffect* damper_ = nullptr;
    IDirectInputEffect* friction_ = nullptr;
    DWORD axes_[1] = {DIJOFS_X};
    LONG direction_[1] = {0};
    std::optional<FoundWheel> wheel_;
    // What it last sent, in DirectInput's steps: it sends only changes.
    LONG sent_force_ = 0;
    LONG sent_damper_ = 0;
    LONG sent_friction_ = 0;
    std::uint64_t reports_ = 0;
};

}  // namespace

std::unique_ptr<WheelOutput> WheelOutput::create() {
    return std::make_unique<WindowsWheelOutput>();
}

void WheelOutput::watch() {}
