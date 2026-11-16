#pragma once

// The vibrations the thread draws on top of the steady force, one sample a
// tick at the rate the wheel takes updates (`rate`, Hz):
// - Tone: a sine, the engine's firing.
// - Texture: the road, a band of noise round a frequency (its width the
//   frequency over kQ), so it feels like a surface going by rather than a
//   motor note, played at an RMS amplitude. It's white noise low-passed to
//   half the band's width (fourth-order Butterworth, so the band's edges are
//   clean) and swung round the frequency (in quadrature),
//   which centres its energy on the frequency: through a band-pass the
//   centre of the energy sits well above it, its skirt reaching further up
//   than down.
// The two add. Neither aliases: a wheel updated `rate` times a second only
// carries waves well below it, so the tone asked above a quarter of the
// rate (the cap) plays at the cap instead, weaker by cap / frequency, and
// the texture's band comes down until its top is at the cap, weaker by as
// much.

#include <cstdint>

// A second-order filter section (the Audio EQ Cookbook's low-pass), in
// transposed direct form II.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    static Biquad low_pass(double frequency, double q, double rate);
    // New coefficients, keeping what it holds, so a sweep doesn't click.
    void retune(const Biquad& to);
    double step(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// The highest frequency a wheel updated `rate` times a second plays.
double vibration_cap(double rate);

class Tone {
public:
    void start(double rate);
    void set(double amplitude, double frequency);
    double step();

private:
    double rate_ = 400.0;
    double amplitude_ = 0.0;
    double frequency_ = 0.0;
    double phase_ = 0.0;
};

class Texture {
public:
    static constexpr double kQ = 0.8;
    // Its noise always starts the same, so a run can be repeated.
    static constexpr std::uint32_t kSeed = 0x9E3779B9u;

    void start(double rate);
    void set(double amplitude, double frequency);
    double step();

private:
    void tune(double centre);

    double noise();

    double rate_ = 400.0;
    double amplitude_ = 0.0;
    double centre_ = 0.0;
    double phase_ = 0.0;
    // What one unit of noise comes out as, RMS, through the low-pass.
    double gain_ = 1.0;
    // The low-pass's two sections, for the in-phase and quadrature noise.
    Biquad in_phase_[2];
    Biquad quadrature_[2];
    std::uint32_t noise_ = kSeed;
};
