#include "vibration.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double kTau = 6.283185307179586;
// The lowest the texture is tuned to: below it, its filter rings longer
// than a texture lasts.
constexpr double kLowest = 1.0;
// Retuned when the centre moves by more than this share: the game sets the
// frequency every physics step.
constexpr double kRetune = 0.005;
// Samples of the filters' impulse response summed for their gain.
constexpr int kGainSamples = 4096;
// Uniform noise from -1 to 1 has this RMS.
const double kNoiseRms = 1.0 / std::sqrt(3.0);
// A fourth-order Butterworth low-pass's two sections.
constexpr double kSectionQ[2] = {0.5411961001461970, 1.3065629648763766};

// The share of a wave at `frequency` that plays, and where.
struct Capped {
    double amplitude;
    double frequency;
};

Capped capped(double amplitude, double frequency, double rate) {
    const double cap = vibration_cap(rate);
    if (frequency <= cap) {
        return {amplitude, frequency};
    }
    return {amplitude * cap / frequency, cap};
}

}  // namespace

Biquad Biquad::low_pass(double frequency, double q, double rate) {
    const double w = kTau * frequency / rate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    const double c = 1.0 - std::cos(w);
    Biquad out;
    out.b0 = c / 2.0 / a0;
    out.b1 = c / a0;
    out.b2 = c / 2.0 / a0;
    out.a1 = -2.0 * std::cos(w) / a0;
    out.a2 = (1.0 - alpha) / a0;
    return out;
}

void Biquad::retune(const Biquad& to) {
    b0 = to.b0;
    b1 = to.b1;
    b2 = to.b2;
    a1 = to.a1;
    a2 = to.a2;
}

double vibration_cap(double rate) {
    return rate / 4.0;
}

void Tone::start(double rate) {
    rate_ = rate;
    amplitude_ = frequency_ = phase_ = 0.0;
}

void Tone::set(double amplitude, double frequency) {
    const Capped wave = capped(std::max(amplitude, 0.0), std::max(frequency, 0.0), rate_);
    amplitude_ = wave.amplitude;
    frequency_ = wave.frequency;
}

double Tone::step() {
    const double value = amplitude_ * std::sin(phase_);
    phase_ = std::fmod(phase_ + kTau * frequency_ / rate_, kTau);
    return value;
}

void Texture::start(double rate) {
    rate_ = rate;
    amplitude_ = 0.0;
    phase_ = 0.0;
    for (int i = 0; i < 2; ++i) {
        in_phase_[i] = quadrature_[i] = Biquad{};
    }
    noise_ = kSeed;
    tune(kLowest);
}

void Texture::set(double amplitude, double frequency) {
    amplitude = std::max(amplitude, 0.0);
    frequency = std::max(frequency, kLowest);
    // The band's top, kept to the cap.
    const double highest = vibration_cap(rate_) / (1.0 + 0.5 / kQ);
    const double centre = std::min(frequency, highest);
    amplitude_ = amplitude * centre / frequency;
    if (std::abs(centre - centre_) > centre_ * kRetune) {
        tune(centre);
    }
}

// The low-pass at half the band's width round `centre`, and the RMS one
// unit of noise comes out at, from the energy of its impulse response.
void Texture::tune(double centre) {
    centre_ = centre;
    Biquad pulse[2];
    for (int i = 0; i < 2; ++i) {
        pulse[i] = Biquad::low_pass(0.5 * centre / kQ, kSectionQ[i], rate_);
        in_phase_[i].retune(pulse[i]);
        quadrature_[i].retune(pulse[i]);
    }
    double energy = 0.0;
    for (int i = 0; i < kGainSamples; ++i) {
        const double y = pulse[1].step(pulse[0].step(i == 0 ? 1.0 : 0.0));
        energy += y * y;
    }
    gain_ = std::sqrt(energy) * kNoiseRms;
}

// xorshift32, from -1 to 1: white, cheap and the same every run.
double Texture::noise() {
    noise_ ^= noise_ << 13;
    noise_ ^= noise_ >> 17;
    noise_ ^= noise_ << 5;
    return noise_ / 2147483648.0 - 1.0;
}

double Texture::step() {
    const double in_phase = in_phase_[1].step(in_phase_[0].step(noise()));
    const double quadrature = quadrature_[1].step(quadrature_[0].step(noise()));
    const double value = in_phase * std::cos(phase_) - quadrature * std::sin(phase_);
    phase_ = std::fmod(phase_ + kTau * centre_ / rate_, kTau);
    return gain_ > 0.0 ? value * amplitude_ / gain_ : 0.0;
}
