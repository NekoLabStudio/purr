// The vibrations drawn in the thread: the road's texture as band-limited
// noise at an RMS amplitude, the engine's tone, and neither playing above
// what the wheel's update rate carries.

#include "test.h"
#include "vibration.h"

#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.141592653589793;

std::vector<double> play(Texture& texture, int samples) {
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        out.push_back(texture.step());
    }
    return out;
}

double rms(const std::vector<double>& samples) {
    double sum = 0.0;
    for (const double x : samples) {
        sum += x * x;
    }
    return std::sqrt(sum / samples.size());
}

// Power by frequency, averaged over Hann-windowed stretches of `length`.
std::vector<double> spectrum(const std::vector<double>& samples, int length) {
    std::vector<double> power(static_cast<std::size_t>(length / 2), 0.0);
    for (std::size_t from = 0; from + length <= samples.size(); from += length) {
        for (int bin = 0; bin < length / 2; ++bin) {
            double re = 0.0;
            double im = 0.0;
            for (int i = 0; i < length; ++i) {
                const double window = 0.5 - 0.5 * std::cos(2.0 * kPi * i / length);
                const double x = samples[from + i] * window;
                re += x * std::cos(2.0 * kPi * bin * i / length);
                im -= x * std::sin(2.0 * kPi * bin * i / length);
            }
            power[bin] += re * re + im * im;
        }
    }
    return power;
}

double centroid(const std::vector<double>& power, double rate, int length) {
    double weighted = 0.0;
    double total = 0.0;
    for (std::size_t bin = 1; bin < power.size(); ++bin) {
        weighted += power[bin] * bin * rate / length;
        total += power[bin];
    }
    return weighted / total;
}

// The share of the power above `frequency`.
double above(const std::vector<double>& power, double frequency, double rate, int length) {
    double high = 0.0;
    double total = 0.0;
    for (std::size_t bin = 1; bin < power.size(); ++bin) {
        total += power[bin];
        if (bin * rate / length > frequency) {
            high += power[bin];
        }
    }
    return high / total;
}

}  // namespace

TEST(texture_plays_its_rms) {
    for (const double rate : {125.0, 250.0, 500.0, 1000.0}) {
        for (const double frequency : {3.0, 12.0, 25.0}) {
            Texture texture;
            texture.start(rate);
            texture.set(0.1, frequency);
            play(texture, 2000);  // settled
            const double got = rms(play(texture, 40000));
            // Weaker only where its band would reach past the cap.
            const double highest = vibration_cap(rate) / (1.0 + 0.5 / Texture::kQ);
            const double expected = 0.1 * std::min(1.0, highest / frequency);
            CHECK(std::abs(got - expected) < 0.1 * expected);
        }
    }
}

TEST(texture_is_centred_on_its_frequency) {
    constexpr int kLength = 512;
    for (const double rate : {500.0, 1000.0}) {
        for (const double frequency : {10.0, 30.0, 60.0}) {
            Texture texture;
            texture.start(rate);
            texture.set(0.1, frequency);
            const std::vector<double> power = spectrum(play(texture, kLength * 24), kLength);
            const double middle = centroid(power, rate, kLength);
            CHECK(std::abs(middle - frequency) < 0.2 * frequency);
        }
    }
}

TEST(texture_never_plays_above_the_cap) {
    // A T300 takes 125 updates a second, so it carries nothing above 31 Hz:
    // paving at 50 km/h (69 Hz) plays as a band whose top is at 31 Hz,
    // centred lower and weaker by as much, not folded back as a false note.
    constexpr int kLength = 256;
    constexpr double kRate = 125.0;
    const double cap = vibration_cap(kRate);
    const double centre = cap / (1.0 + 0.5 / Texture::kQ);
    Texture texture;
    texture.start(kRate);
    texture.set(0.1, 69.0);
    play(texture, 1000);
    const std::vector<double> samples = play(texture, kLength * 40);
    const double expected = 0.1 * centre / 69.0;
    CHECK(std::abs(rms(samples) - expected) < 0.1 * expected);
    const std::vector<double> power = spectrum(samples, kLength);
    CHECK(above(power, cap, kRate, kLength) < 0.06);  // the band's soft edge
    CHECK(above(power, 1.5 * cap, kRate, kLength) < 0.001);
    CHECK(std::abs(centroid(power, kRate, kLength) - centre) < 0.2 * centre);
}

TEST(texture_is_the_same_every_run) {
    Texture a;
    Texture b;
    a.start(500.0);
    b.start(500.0);
    a.set(0.05, 20.0);
    b.set(0.05, 20.0);
    CHECK(play(a, 100) == play(b, 100));
}

TEST(tone_is_a_sine_and_capped) {
    Tone tone;
    tone.start(500.0);
    tone.set(0.2, 25.0);
    double peak = 0.0;
    int crossings = 0;
    double last = tone.step();
    for (int i = 1; i < 500; ++i) {
        const double x = tone.step();
        peak = std::max(peak, std::abs(x));
        crossings += (last < 0.0) != (x < 0.0);
        last = x;
    }
    CHECK(std::abs(peak - 0.2) < 0.005);
    CHECK(std::abs(crossings - 50) <= 1);  // 25 Hz for a second
    // Above the cap (125 Hz at 500 updates a second) it plays at the cap, weaker.
    tone.set(0.2, 250.0);
    peak = 0.0;
    for (int i = 0; i < 100; ++i) {
        peak = std::max(peak, std::abs(tone.step()));
    }
    CHECK(std::abs(peak - 0.1) < 0.005);
}
