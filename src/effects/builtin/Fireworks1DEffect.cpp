// Fireworks 1D: rockets rise, slow down and burst into round shells of sparks.
//
// Based on "Fireworks 1D" in WLED (wled00/FX.cpp, mode_exploding_fireworks; from
// http://www.anirama.com/1000leds/1d-fireworks/, adapted for 2D WLED by blazoncek). The
// rocket flight, the options and the burn from white through the color to red follow
// WLED; the burst is made to look like a real shell: WLED's sparks fly out mostly
// sideways, wider on the right of the matrix, and all bursts are alike.
//
// On a matrix a rocket starts at the bottom in a random column (not the two at each
// edge) and rises to 50-85 % of the height, slowing down. At the top of its flight it
// bursts with a random strength: the sparks fly out evenly in all directions (angles
// spread around the circle, nearly equal speeds), so the burst grows as a round shell.
// A stronger burst is larger and has more sparks. Air drag slows the sparks and gravity
// pulls them down, so the shell spreads, stops and droops. Every spark flashes white,
// burns in the burst's color (sparks differ a little), dims and reddens as it cools and
// crackles before it goes out, each after its own time. A new rocket starts every
// 0.5-1.9 s, up to three fireworks at once. On a strip (one pixel wide or high) the
// rocket flies along it, from either end (firing side), and the burst spreads both
// ways along it. Each simulation step (one frame in WLED, about 42 per second) dims the
// whole matrix (short trails), moves everything and draws it. Steps run at a fixed rate
// counted from deltaMs, so the fireworks look the same at any FPS.
//
// The rocket and the sparks are kept in fractions of a pixel and drawn as soft dots
// (render/SoftDraw.h), so on a small matrix they glide instead of jumping.
//   gravity  how strongly the rocket and the sparks fall (WLED speed)
//   side     strip only: chance out of 255 that a rocket starts from the far end
//   colors   palette: a random palette color per burst; color: every burst in color
//   color    spark color for colors = color (WLED's default primary color)
//   size     rocket and spark radius, in tenths of a pixel. 1.3 by default: a point
//            between four pixels still gives each half its light (a quarter at 1.0)
//   palette  burst colors
// Randomness comes from FastLED random8() and random16(); App seeds them at start.
#include <lib8tion.h>

#include <algorithm>
#include <cmath>

#include "effects/EffectRegistry.h"
#include "render/SoftDraw.h"

namespace {

using ledframe::Canvas;
using ledframe::drawSoftDot;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kGravity, kSide, kColors, kColor, kSize, kPalette };
enum Colors : int32_t { kColorsPalette, kColorsColor };

constexpr ParamSpec kParams[] = {
    {"gravity", "Gravity", ParamKind::Slider, 0, 255, 128},
    {"side", "Firing side", ParamKind::Slider, 0, 255, 128},
    {"colors", "Colors", ParamKind::Select, 0, 0, kColorsPalette, "palette|color"},
    {"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFFA000},
    {"size", "Size", ParamKind::Slider, 10, 20, 13},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr uint32_t kStepMs = 24;          // about 42 steps per second, the WLED frame rate
constexpr uint8_t kMaxStepsPerFrame = 8;  // after a stall only the last steps are drawn
constexpr uint8_t kFade = 72;             // trail fade per step, out of 256
constexpr float kDrag = 0.94f;            // spark speed kept per step (air drag)
constexpr uint8_t kFlashSteps = 3;        // a spark starts white for this many steps
constexpr int kMaxShells = 3;             // fireworks at once
constexpr uint8_t kMinPause = 20;         // steps between launches: 20-79 (0.5-1.9 s)
constexpr uint8_t kPauseRange = 60;

struct Spark {
  float pos;     // along the flight, from the launching end
  float across;  // across the flight (matrix column)
  float vel;     // along, pixels per step
  float velAcross;
  uint8_t age;   // steps since the burst (the rocket: brightness)
  uint8_t life;  // steps until the spark goes out; 0: not in use
  uint8_t colorIndex;
};

// Sparks per shell. They live in the effect slot, so their number is limited by the
// slot size (docs/ADDING_EFFECT.md, section 3); 24 fill a 15 x 10 matrix well.
constexpr size_t kSparksPerShell =
    std::min<size_t>(24, (ledframe::kEffectSlotSize - 256) / sizeof(Spark) / kMaxShells);
static_assert(kSparksPerShell >= 6, "the effect slot is too small for a burst");

enum class Phase : uint8_t { kIdle, kRocket, kBurning };

struct Shell {
  Spark rocket;
  Spark sparks[kSparksPerShell];
  uint8_t count;  // sparks in use
  Phase phase;
  bool reversed;  // strip: launched from the far end
};

float random01() {
  return static_cast<float>(random16()) / 65535.0f;
}

class Fireworks1DEffect : public Effect {
 public:
  void begin(FrameContext& /*ctx*/) override {
    for (Shell& shell : shells_) {
      shell.phase = Phase::kIdle;
    }
    untilLaunch_ = 0;  // the first rocket starts at once
    carryMs_ = 0;
  }

  bool render(FrameContext& ctx) override {
    carryMs_ += ctx.deltaMs;
    uint8_t steps = 0;
    while (carryMs_ >= kStepMs) {
      carryMs_ -= kStepMs;
      // Every step moves the fireworks; only the last ones of a stall are drawn, so a
      // long stall does not draw hundreds of steps in one frame.
      const bool draw = carryMs_ < kStepMs * kMaxStepsPerFrame;
      step(ctx, draw);
      steps += draw ? 1 : 0;
    }
    return steps > 0;  // no step: the same picture as in the last frame
  }

 private:
  void step(FrameContext& ctx, bool draw) {
    Canvas& canvas = ctx.canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    // A strip (one pixel wide or high) is WLED's 1D: the flight runs along it. A
    // matrix is WLED's 2D: the flight runs up the columns.
    strip_ = width == 1 || height == 1;
    length_ = strip_ ? (width > height ? width : height) : height;  // WLED: rows
    across_ = strip_ ? 1 : width;                                   // WLED: cols
    radius_ = static_cast<float>(ctx.params[kSize]) / 10.0f;
    if (draw) {
      canvas.fadeToBlackBy(kFade);
    }

    // WLED: (-0.0004 - speed / 800000) * rows pixels per frame squared.
    const float gravity =
        (-0.0004f - static_cast<float>(ctx.params[kGravity]) / 800000.0f) * length_;
    if (untilLaunch_ > 0) {
      --untilLaunch_;
    }
    if (untilLaunch_ == 0) {
      for (Shell& shell : shells_) {
        if (shell.phase == Phase::kIdle) {
          launch(shell, gravity, ctx.params[kSide]);
          untilLaunch_ = static_cast<uint8_t>(kMinPause + random8(kPauseRange));
          break;
        }
      }
    }

    const bool oneColor = ctx.params[kColors] == kColorsColor;
    const CRGB userColor = ctx.userColor(ctx.params[kColor]);
    for (Shell& shell : shells_) {
      if (shell.phase == Phase::kRocket) {
        fly(canvas, shell, gravity, draw);
      } else if (shell.phase == Phase::kBurning) {
        burn(ctx, shell, gravity, draw, oneColor, userColor);
      }
    }
  }

  void launch(Shell& shell, float gravity, int32_t side) {
    Spark& rocket = shell.rocket;
    rocket.pos = 0.0f;
    if (strip_) {
      shell.reversed = side > random8();  // WLED: posX = intensity > random8()
      rocket.across = 0.0f;
      rocket.velAcross = 0.0f;
    } else {
      shell.reversed = false;
      // WLED: random16(2, cols - 3), the two columns at each edge left out.
      const int low = across_ > 4 ? 2 : 0;
      const int high = across_ > 4 ? across_ - 3 : across_;
      rocket.across =
          static_cast<float>(random16(static_cast<uint16_t>(low), static_cast<uint16_t>(high)));
      rocket.velAcross = static_cast<float>(static_cast<int>(random8(9)) - 4) / 64.0f;
    }
    // Peak at 50-85 % of the flight, so that the shell has room around it.
    const float peak = (0.5f + 0.35f * random01()) * static_cast<float>(length_ - 1);
    rocket.vel = std::sqrt(-2.0f * gravity * peak);
    rocket.age = 255;  // brightness
    shell.phase = Phase::kRocket;
  }

  void fly(Canvas& canvas, Shell& shell, float gravity, bool draw) {
    Spark& rocket = shell.rocket;
    if (rocket.vel <= 12.0f * gravity) {  // WLED: just past the top of the flight
      burst(shell);
      return;
    }
    if (draw) {
      dot(canvas, shell, rocket.pos, rocket.across, CRGB(rocket.age, rocket.age, rocket.age));
    }
    rocket.pos = std::clamp(rocket.pos + rocket.vel, 0.0f, static_cast<float>(length_ - 1));
    if (!strip_) {
      rocket.across =
          std::clamp(rocket.across + rocket.velAcross, 0.0f, static_cast<float>(across_ - 1));
    }
    rocket.vel += gravity;
    rocket.age = rocket.age >= 2 ? static_cast<uint8_t>(rocket.age - 2) : 0;
  }

  void burst(Shell& shell) {
    // Random strength: the size of the shell and the number of its sparks.
    const float strength = 0.45f + 0.55f * random01();
    const int shorter = strip_ ? length_ / 2 : std::min(across_, length_);
    // Without gravity a spark of speed v travels v / (1 - drag); the strongest shell
    // reaches half the shorter side of the matrix (a fifth of a strip).
    const float reach = strength * static_cast<float>(shorter) * (strip_ ? 0.4f : 0.5f);
    const float speed = reach * (1.0f - kDrag);
    const size_t count = std::max<size_t>(
        6, static_cast<size_t>(strength * static_cast<float>(kSparksPerShell) + 0.5f));
    shell.count = static_cast<uint8_t>(std::min(count, kSparksPerShell));
    const uint8_t color = random8();
    const uint32_t turn = random16();  // the shell's rotation
    for (size_t i = 0; i < shell.count; ++i) {
      Spark& spark = shell.sparks[i];
      spark.pos = shell.rocket.pos;
      spark.across = shell.rocket.across;
      // Even angles around the circle, each moved by up to a quarter of the gap; nearly
      // equal speeds, so the sparks form a round shell.
      const uint32_t gap = 65536u / shell.count;
      const uint16_t angle = static_cast<uint16_t>(
          turn + i * gap + random16(static_cast<uint16_t>(gap / 2)) - gap / 4);
      const float v = speed * (0.85f + 0.15f * random01());
      const float cosine = static_cast<float>(cos16(angle)) / 32767.0f;
      const float sine = static_cast<float>(sin16(angle)) / 32767.0f;
      // A strip sees the shell from the side: only the speed along it.
      spark.vel = strip_ ? v * cosine : v * sine;
      spark.velAcross = strip_ ? 0.0f : v * cosine;
      spark.age = 0;
      spark.life = static_cast<uint8_t>(40 + random8(40));  // 1.0-1.9 s
      spark.colorIndex = static_cast<uint8_t>(color + random8(17) - 8);
    }
    shell.phase = Phase::kBurning;
  }

  void burn(FrameContext& ctx, Shell& shell, float gravity, bool draw, bool oneColor,
            const CRGB& userColor) {
    bool burning = false;
    for (size_t i = 0; i < shell.count; ++i) {
      Spark& spark = shell.sparks[i];
      if (spark.age >= spark.life) {
        continue;
      }
      burning = true;
      spark.pos += spark.vel;
      spark.across += spark.velAcross;
      spark.vel = spark.vel * kDrag + gravity;
      spark.velAcross *= kDrag;
      ++spark.age;
      const bool inside = spark.pos > -1.0f && spark.pos < static_cast<float>(length_) &&
                          spark.across > -1.0f && spark.across < static_cast<float>(across_);
      if (draw && inside) {
        const CRGB color = oneColor
                               ? userColor
                               : ColorFromPalette(ctx.palette, spark.colorIndex, 255, LINEARBLEND);
        dot(ctx.canvas, shell, spark.pos, spark.across, sparkColor(spark, color));
      }
    }
    if (!burning) {
      shell.phase = Phase::kIdle;
    }
  }

  // Color of a spark by its age: a white flash, then its color at full light for the
  // first 40 % of its life, then dimming linearly in perceived brightness (squared for
  // the LEDs), reddening as it cools and crackling at random in the last 40 %.
  static CRGB sparkColor(const Spark& spark, const CRGB& color) {
    if (spark.age <= kFlashSteps) {
      const uint8_t white = static_cast<uint8_t>(255 - spark.age * 255 / (kFlashSteps + 1));
      return CRGB(static_cast<uint8_t>(color.r + (255 - color.r) * white / 255),
                  static_cast<uint8_t>(color.g + (255 - color.g) * white / 255),
                  static_cast<uint8_t>(color.b + (255 - color.b) * white / 255));
    }
    const float left = 1.0f - static_cast<float>(spark.age) / static_cast<float>(spark.life);
    float perceived = left < 0.6f ? left / 0.6f : 1.0f;  // full for the first 40 %
    if (left < 0.4f) {
      perceived *= 0.55f + 0.45f * random01();
    }
    const uint8_t level = static_cast<uint8_t>(perceived * perceived * 255.0f);
    const uint8_t cooling = static_cast<uint8_t>((1.0f - left) * 48.0f);
    return CRGB(scale8(color.r, level), qsub8(scale8(color.g, level), cooling),
                qsub8(scale8(color.b, level), static_cast<uint8_t>(cooling * 2)));
  }

  // Draws a soft dot at a place of the flight: on a matrix the flight runs up from the
  // bottom row; on a strip along it, from its start or, reversed, from its end.
  void dot(Canvas& canvas, const Shell& shell, float pos, float across, const CRGB& color) const {
    if (color.r == 0 && color.g == 0 && color.b == 0) {
      return;
    }
    const float last = static_cast<float>(length_ - 1);
    float x;
    float y;
    if (!strip_) {
      x = across;
      y = last - pos;
    } else if (canvas.height() == 1) {
      x = shell.reversed ? last - pos : pos;
      y = 0.0f;
    } else {
      x = 0.0f;
      y = shell.reversed ? pos : last - pos;
    }
    drawSoftDot(canvas, x, y, color, radius_);
  }

  Shell shells_[kMaxShells] = {};
  float radius_ = 1.0f;
  int length_ = 1;
  int across_ = 1;
  uint32_t carryMs_ = 0;
  uint8_t untilLaunch_ = 0;
  bool strip_ = false;
};

}  // namespace

REGISTER_EFFECT(Fireworks1DEffect, "fireworks-1d", "Fireworks 1D", 66,
                ledframe::kFlag2D | ledframe::kFlagPalette, kParams);
