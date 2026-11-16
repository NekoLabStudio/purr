// A wheel's hat and buttons on Windows: DirectInput's first POV and its
// buttons, read from a device of its own taken without excluding anyone
// (force feedback holds the wheel exclusively through another), in the
// background.

#include "hat_reader.h"

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include <vector>

namespace {

// Godot's hat mask by POV eighth, clockwise from up.
constexpr int kEighths[] = {1, 1 | 2, 2, 2 | 4, 4, 4 | 8, 8, 8 | 1};

BOOL CALLBACK take_each(LPCDIDEVICEINSTANCEW instance, LPVOID found) {
    static_cast<std::vector<DIDEVICEINSTANCEW>*>(found)->push_back(*instance);
    return DIENUM_CONTINUE;
}

class WindowsHatReader : public HatReader {
public:
    WindowsHatReader(IDirectInput8W* input, IDirectInputDevice8W* device, DWORD buttons)
        : input_(input), device_(device), buttons_(buttons > 128 ? 128 : buttons) {}

    ~WindowsHatReader() override {
        device_->Unacquire();
        device_->Release();
        input_->Release();
    }

    std::optional<int> hat() override {
        const std::optional<DIJOYSTATE2> state = read();
        if (!state) {
            return std::nullopt;
        }
        const DWORD pov = state->rgdwPOV[0];
        if (LOWORD(pov) == 0xFFFF) {
            return 0;  // centred
        }
        return kEighths[((pov + 2250) % 36000) / 4500];  // hundredths of a degree, clockwise from up
    }

    bool reads_buttons() const override { return true; }

    std::optional<std::vector<int>> buttons() override {
        const std::optional<DIJOYSTATE2> state = read();
        if (!state) {
            return std::nullopt;
        }
        std::vector<int> held;
        for (DWORD i = 0; i < buttons_; ++i) {
            if (state->rgbButtons[i] & 0x80) {
                held.push_back(static_cast<int>(i));
            }
        }
        return held;
    }

private:
    std::optional<DIJOYSTATE2> read() {
        if (FAILED(device_->Poll()) && (FAILED(device_->Acquire()) || FAILED(device_->Poll()))) {
            return std::nullopt;
        }
        DIJOYSTATE2 state{};
        if (FAILED(device_->GetDeviceState(sizeof(state), &state))) {
            return std::nullopt;
        }
        return state;
    }

    IDirectInput8W* input_;
    IDirectInputDevice8W* device_;
    DWORD buttons_;
};

}  // namespace

std::unique_ptr<HatReader> HatReader::open(std::uint16_t vendor_id, std::uint16_t product_id, std::uintptr_t window) {
    IDirectInput8W* input = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                  reinterpret_cast<void**>(&input), nullptr))) {
        return nullptr;
    }
    std::vector<DIDEVICEINSTANCEW> found;
    input->EnumDevices(DI8DEVCLASS_GAMECTRL, take_each, &found, DIEDFL_ATTACHEDONLY);
    for (const DIDEVICEINSTANCEW& each : found) {
        // Its product GUID's first field is MAKELONG(vendor, product).
        if (LOWORD(each.guidProduct.Data1) != vendor_id || HIWORD(each.guidProduct.Data1) != product_id) {
            continue;
        }
        IDirectInputDevice8W* device = nullptr;
        if (FAILED(input->CreateDevice(each.guidInstance, &device, nullptr))) {
            continue;
        }
        DIDEVCAPS caps{};
        caps.dwSize = sizeof(caps);
        if (SUCCEEDED(device->GetCapabilities(&caps)) && caps.dwPOVs > 0 && SUCCEEDED(device->SetDataFormat(&c_dfDIJoystick2)) &&
            SUCCEEDED(device->SetCooperativeLevel(reinterpret_cast<HWND>(window), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND))) {
            device->Acquire();
            return std::make_unique<WindowsHatReader>(input, device, caps.dwButtons);
        }
        device->Release();
    }
    input->Release();
    return nullptr;
}
