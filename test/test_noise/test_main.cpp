#include <unity.h>

#include <cstdlib>
#include <vector>

#include "effects/EffectRegistry.h"
#include "render/Palettes.h"

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::Palettes;

namespace {

enum P : uint8_t { kSpeed, kScale, kPalette };

Canvas canvas;
Palettes palettes;
// Black to white: the brightness of a pixel is its palette index, so tests can read
// the noise from the canvas.
CRGBPalette16 gray;

// One noise effect with its own parameter values on the gray palette.
struct Noise {
  Noise() {
    descriptor = EffectRegistry::global().find("noise");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
    FrameContext ctx = context(0);
    effect->begin(ctx);
  }

  FrameContext context(uint32_t nowMs) { return FrameContext{canvas, nowMs, 20, params, gray}; }
  bool render(uint32_t nowMs) {
    FrameContext ctx = context(nowMs);
    return effect->render(ctx);
  }
  void change(uint8_t index, int32_t value) {
    params[index] = value;
    FrameContext ctx = context(0);
    effect->onParamChanged(index, ctx);
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
};

// Brightness (green channel of the gray palette) of every pixel, row by row.
std::vector<int> frameAt(Noise& noise, uint32_t nowMs) {
  noise.render(nowMs);
  std::vector<int> out;
  for (int i = 0; i < canvas.size(); ++i) {
    out.push_back(canvas[i].g);
  }
  return out;
}

// Mean absolute difference between two frames.
double difference(const std::vector<int>& a, const std::vector<int>& b) {
  double sum = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    sum += std::abs(a[i] - b[i]);
  }
  return sum / a.size();
}

// The vertical shift (rows, negative = up) that best maps frame a onto frame b.
int bestShift(const std::vector<int>& a, const std::vector<int>& b) {
  const int width = canvas.width();
  const int height = canvas.height();
  int best = 0;
  double bestScore = 1e9;
  for (int shift = -5; shift <= 5; ++shift) {
    double sum = 0;
    int count = 0;
    for (int y = 0; y < height; ++y) {
      if (y + shift < 0 || y + shift >= height) {
        continue;
      }
      for (int x = 0; x < width; ++x) {
        sum += std::abs(a[y * width + x] - b[(y + shift) * width + x]);
        ++count;
      }
    }
    if (sum / count < bestScore) {
      bestScore = sum / count;
      best = shift;
    }
  }
  return best;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t level = static_cast<uint8_t>(i * 17);
    gray.entries[i] = CRGB(level, level, level);
  }
}
void tearDown() {}

void test_noise_is_registered() {
  const EffectDescriptor* noise = EffectRegistry::global().find("noise");
  TEST_ASSERT_NOT_NULL(noise);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(3, noise->paramCount);
  TEST_ASSERT_EQUAL_INT32(4, noise->params[kPalette].def);  // lava
  TEST_ASSERT_EQUAL_INT(kSpeed, ledframe::findParam(*noise, "speed"));
}

void test_noise_is_deterministic() {
  Noise a;
  Noise b;
  TEST_ASSERT_TRUE(frameAt(a, 2500) == frameAt(b, 2500));
}

// Neighbors differ little (soft blobs, not random dots), but the picture is not flat.
void test_noise_is_smooth_but_varied() {
  Noise noise;
  const std::vector<int> frame = frameAt(noise, 1000);
  const int width = canvas.width();
  double neighbor = 0;
  int pairs = 0;
  int low = 255;
  int high = 0;
  for (int i = 0; i < canvas.size(); ++i) {
    if (i % width + 1 < width) {
      neighbor += std::abs(frame[i] - frame[i + 1]);
      ++pairs;
    }
    low = frame[i] < low ? frame[i] : low;
    high = frame[i] > high ? frame[i] : high;
  }
  TEST_ASSERT_TRUE(neighbor / pairs < 30);
  TEST_ASSERT_TRUE(high - low > 80);
}

// The stretch maps the usual noise range over almost the whole palette.
void test_noise_uses_the_whole_palette() {
  Noise noise;
  int low = 255;
  int high = 0;
  for (uint32_t ms = 0; ms < 20000; ms += 500) {
    for (int value : frameAt(noise, ms)) {
      low = value < low ? value : low;
      high = value > high ? value : high;
    }
  }
  TEST_ASSERT_TRUE(low < 30);
  TEST_ASSERT_TRUE(high > 225);
}

// The blobs rise: speed 40 at scale 30 moves the picture about 2.7 rows per second.
void test_noise_rises() {
  TEST_ASSERT_TRUE(canvas.resize(15, 20));
  Noise noise;
  int up = 0;
  for (uint32_t ms = 0; ms < 8000; ms += 1000) {
    const std::vector<int> before = frameAt(noise, ms);
    const int shift = bestShift(before, frameAt(noise, ms + 1000));
    up += shift <= -2 && shift >= -4 ? 1 : 0;
  }
  TEST_ASSERT_TRUE(up >= 6);  // at least 6 of 8 seconds
}

void test_noise_speed_0_is_still() {
  Noise noise;
  noise.change(kSpeed, 0);
  TEST_ASSERT_TRUE(noise.render(0));
  const std::vector<int> still = frameAt(noise, 0);
  TEST_ASSERT_FALSE(noise.render(9000));
  noise.change(kScale, 60);
  TEST_ASSERT_TRUE(noise.render(9000));
  TEST_ASSERT_TRUE(still != frameAt(noise, 9000));
}

// The rise coordinate wraps at 65536 noise units (speed 255: after 128.5 s). The
// noise repeats there, so two frames 40 ms apart across the wrap differ as little as
// any other two neighboring frames.
void test_noise_has_no_jump_at_the_wrap() {
  Noise noise;
  noise.change(kSpeed, 255);
  const double normal = difference(frameAt(noise, 1000), frameAt(noise, 1040));
  const double wrap = difference(frameAt(noise, 128480), frameAt(noise, 128520));
  TEST_ASSERT_TRUE(wrap < normal * 2 + 5);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_noise_is_registered);
  RUN_TEST(test_noise_is_deterministic);
  RUN_TEST(test_noise_is_smooth_but_varied);
  RUN_TEST(test_noise_uses_the_whole_palette);
  RUN_TEST(test_noise_rises);
  RUN_TEST(test_noise_speed_0_is_still);
  RUN_TEST(test_noise_has_no_jump_at_the_wrap);
  return UNITY_END();
}
