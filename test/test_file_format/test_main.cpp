// File formats through the byte streams. The expected bytes were written by the
// buffer serializers of 2.0.0, before files were streamed, so the streams must keep
// every file readable and byte for byte the same. Fields added later (dither and
// gamma in 2.0.1) have their own expected bytes; the old files must still read.
#include <unity.h>

#include <cstring>
#include <string>

#include "core/ByteStream.h"
#include "effects/PaintImage.h"
#include "state/Config.h"
#include "state/Presets.h"
#include "state/State.h"

using ledframe::ArrayReader;
using ledframe::Config;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::IByteWriter;
using ledframe::Origin;
using ledframe::PaintImage;
using ledframe::PaintOp;
using ledframe::ParamKind;
using ledframe::ParamSpec;
using ledframe::Presets;
using ledframe::Rotation;
using ledframe::State;

namespace {

class NullEffect : public Effect {
 public:
  bool render(FrameContext& /*ctx*/) override { return false; }
};
Effect* makeNull(void* memory) {
  return new (memory) NullEffect();
}

constexpr ParamSpec kSolidParams[] = {{"color", "Color", ParamKind::Color, 0, 0xFFFFFF, 0xFFFFFF}};
constexpr ParamSpec kLevelParams[] = {
    {"level", "Level", ParamKind::Slider, 0, 100, 50},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};
const EffectDescriptor kSolid = {"solid", "Solid", 1, 0, kSolidParams, 1, nullptr, &makeNull};
const EffectDescriptor kLevel = {"level", "Level", 2, 0, kLevelParams, 2, nullptr, &makeNull};
const EffectDescriptor kPlain = {"plain", "Plain", 3, 0, nullptr, 0, nullptr, &makeNull};

const EffectRegistry& registry() {
  static EffectRegistry r;
  static bool filled = false;
  if (!filled) {
    r.add(&kSolid);
    r.add(&kLevel);
    r.add(&kPlain);
    filled = true;
  }
  return r;
}

// Collects everything written, like a file.
class StringWriter : public IByteWriter {
 public:
  using IByteWriter::write;
  size_t write(const uint8_t* data, size_t length) override {
    text.append(reinterpret_cast<const char*>(data), length);
    return length;
  }
  std::string text;
};

// Takes `limit` bytes, then reports every write as failed, like a full flash.
class FullWriter : public IByteWriter {
 public:
  explicit FullWriter(size_t limit) : limit_(limit) {}
  using IByteWriter::write;
  size_t write(const uint8_t* /*data*/, size_t length) override {
    if (taken_ + length > limit_) {
      return 0;
    }
    taken_ += length;
    return length;
  }

 private:
  size_t limit_;
  size_t taken_ = 0;
};

// --- Files written by 2.0.0 ---------------------------------------------------------

const char kConfigJson[] =
    "{\"layout\":{\"width\":20,\"height\":10,\"serpentine\":true,\"origin\":\"bottom-right\","
    "\"rotation\":90,\"flipX\":true,\"flipY\":false},\"colorOrder\":2,\"power\":{\"volts\":5,"
    "\"milliamps\":0},\"fps\":60,\"hostname\":\"frame-1\",\"wifi\":{\"ssid\":\"Cafe "
    "\\\"free\\\"\",\"password\":\"secret12\"},\"ota\":{\"password\":\"\"}}";

// The same configuration as written now: "ledPin" follows "colorOrder", "dither" and
// "gamma" (2.0.1) follow "fps", "timezone" and "ntpServer" and "schedule" (2.2.0)
// come last.
const char kConfigJsonCurrent[] =
    "{\"layout\":{\"width\":20,\"height\":10,\"serpentine\":true,\"origin\":\"bottom-right\","
    "\"rotation\":90,\"flipX\":true,\"flipY\":false},\"colorOrder\":2,\"ledPin\":2,"
    "\"power\":{\"volts\":5,\"milliamps\":0},\"fps\":60,\"dither\":true,\"gamma\":true,\"hostname\":\"frame-1\","
    "\"wifi\":{\"ssid\":\"Cafe \\\"free\\\"\",\"password\":\"secret12\"},"
    "\"ota\":{\"password\":\"\"},\"timezone\":\"CET-1CEST,M3.5.0,M10.5.0/3\","
    "\"ntpServer\":\"pool.ntp.org\",\"schedule\":[]}";

const char kStateJson[] =
    "{\"on\":false,\"bri\":77,\"effect\":\"level\",\"transition\":300,\"palette\":3,"
    "\"userColors\":[16711680,32768,255],\"params\":{\"solid\":{\"color\":1193046},"
    "\"level\":{\"level\":42,\"palette\":4}}}";

const char kPresetsJson[] =
    "{\"nextId\":3,\"presets\":[{\"id\":1,\"name\":\"Wiecz\xC3\xB3r\",\"effect\":\"level\","
    "\"bri\":60,\"palette\":2,\"params\":{\"level\":30,\"palette\":2}},{\"id\":2,\"name\":"
    "\"Red\",\"effect\":\"solid\",\"bri\":255,\"palette\":0,\"params\":{\"color\":16711680}}]}";

const uint8_t kPaintBin[] = {'L', 'P', 1, 0, 3, 0, 2, 0, 0, 0, 0, 0, 0,
                             0,   0,   0, 0, 0, 0, 0, 1, 2, 3, 0, 0, 0};

Config sampleConfig() {
  Config config;
  config.layout.width = 20;
  config.layout.serpentine = true;
  config.layout.origin = Origin::kBottomRight;
  config.layout.rotation = Rotation::k90;
  config.layout.flipX = true;
  config.colorOrder = 2;
  config.fps = 60;
  config.powerMilliamps = 0;
  std::strcpy(config.hostname, "frame-1");
  std::strcpy(config.wifiSsid, "Cafe \"free\"");
  std::strcpy(config.wifiPassword, "secret12");
  return config;
}

State sampleState() {
  State state;
  state.reset(registry());
  state.on = false;
  state.brightness = 77;
  state.effect = 1;
  state.transitionMs = 300;
  state.palette = 3;
  state.params[0][0] = 0x123456;
  state.params[1][0] = 42;
  state.params[1][1] = 4;
  return state;
}

void samplePresets(Presets& presets) {
  presets.clear();
  const int32_t level[] = {30, 2};
  const int32_t solid[] = {0xFF0000};
  presets.save(ledframe::makePreset("Wiecz\xC3\xB3r", 1, level, 2, 2, 60));
  presets.save(ledframe::makePreset("Red", 0, solid, 1, 0, 255));
}

void samplePaint(PaintImage& image) {
  image.resize(3, 2);
  PaintOp op;
  op.x = 1;
  op.y = 1;
  op.color = CRGB(1, 2, 3);
  image.apply(op);
}

void expectSampleState(const State& state) {
  TEST_ASSERT_FALSE(state.on);
  TEST_ASSERT_EQUAL_UINT8(77, state.brightness);
  TEST_ASSERT_EQUAL_UINT8(1, state.effect);
  TEST_ASSERT_EQUAL_UINT32(300, state.transitionMs);
  TEST_ASSERT_EQUAL_INT32(3, state.palette);
  TEST_ASSERT_EQUAL_INT32(0x123456, state.params[0][0]);
  TEST_ASSERT_EQUAL_INT32(42, state.params[1][0]);
  TEST_ASSERT_EQUAL_INT32(4, state.params[1][1]);
}

void expectDefaultState(const State& state) {
  State defaults;
  defaults.reset(registry());
  TEST_ASSERT_EQUAL(defaults.on, state.on);
  TEST_ASSERT_EQUAL_UINT8(defaults.brightness, state.brightness);
  TEST_ASSERT_EQUAL_UINT8(defaults.effect, state.effect);
  TEST_ASSERT_EQUAL_INT32(defaults.params[1][0], state.params[1][0]);
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Writing: the stream gives the bytes of the old buffer serializers ------------------

void test_config_bytes() {
  const Config config = sampleConfig();
  StringWriter writer;
  TEST_ASSERT_EQUAL_UINT32(std::strlen(kConfigJsonCurrent),
                           ledframe::serializeConfig(config, writer));
  TEST_ASSERT_EQUAL_STRING(kConfigJsonCurrent, writer.text.c_str());
  char buffer[512];
  TEST_ASSERT_EQUAL_UINT32(std::strlen(kConfigJsonCurrent),
                           ledframe::serializeConfig(config, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING(kConfigJsonCurrent, buffer);
}

void test_state_bytes() {
  const State state = sampleState();
  StringWriter writer;
  ledframe::serializeState(state, registry(), writer);
  TEST_ASSERT_EQUAL_STRING(kStateJson, writer.text.c_str());
  char buffer[512];
  TEST_ASSERT_EQUAL_UINT32(std::strlen(kStateJson),
                           ledframe::serializeState(state, registry(), buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING(kStateJson, buffer);
}

void test_presets_bytes() {
  static Presets presets;
  samplePresets(presets);
  StringWriter writer;
  ledframe::serializePresets(presets, registry(), writer);
  TEST_ASSERT_EQUAL_STRING(kPresetsJson, writer.text.c_str());
  char buffer[512];
  TEST_ASSERT_EQUAL_UINT32(std::strlen(kPresetsJson),
                           ledframe::serializePresets(presets, registry(), buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING(kPresetsJson, buffer);
}

void test_paint_bytes_in_parts() {
  static PaintImage image;
  samplePaint(image);
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPaintBin), image.serializedSize());
  // Parts of 5 bytes cross the header and pixel boundaries, like App's saving.
  uint8_t bytes[sizeof(kPaintBin)];
  size_t offset = 0;
  size_t count = 0;
  while ((count = image.serializePart(offset, bytes + offset, 5)) > 0) {
    offset += count;
  }
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPaintBin), offset);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kPaintBin, bytes, sizeof(kPaintBin));
  TEST_ASSERT_EQUAL_UINT32(0, image.serializePart(sizeof(kPaintBin), bytes, sizeof(bytes)));
  uint8_t whole[64];
  TEST_ASSERT_EQUAL_UINT32(sizeof(kPaintBin), image.serialize(whole, sizeof(whole)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kPaintBin, whole, sizeof(kPaintBin));
}

// The buffer form needs room for the terminating '\0'.
void test_buffer_needs_room_for_terminator() {
  const Config config = sampleConfig();
  const size_t length = std::strlen(kConfigJsonCurrent);
  char buffer[512];
  TEST_ASSERT_EQUAL_UINT32(length, ledframe::serializeConfig(config, buffer, length + 1));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::serializeConfig(config, buffer, length));
  TEST_ASSERT_EQUAL_UINT32(0, ledframe::serializeConfig(config, nullptr, 0));
}

// A failing write shows as a short count, so Storage keeps the old file.
void test_write_error_is_reported() {
  FullWriter writer(40);
  TEST_ASSERT_TRUE(ledframe::serializeConfig(sampleConfig(), writer) < std::strlen(kConfigJson));
}

// --- Reading files written by 2.0.0 ------------------------------------------------------

void test_read_old_files() {
  Config config;
  ArrayReader configReader(kConfigJson, std::strlen(kConfigJson));
  TEST_ASSERT_TRUE(ledframe::deserializeConfig(configReader, config));
  TEST_ASSERT_TRUE(config == sampleConfig());

  State state;
  ArrayReader stateReader(kStateJson, std::strlen(kStateJson));
  TEST_ASSERT_TRUE(ledframe::deserializeState(stateReader, registry(), state));
  expectSampleState(state);

  static Presets presets;
  ArrayReader presetsReader(kPresetsJson, std::strlen(kPresetsJson));
  TEST_ASSERT_TRUE(ledframe::deserializePresets(presetsReader, registry(), presets));
  TEST_ASSERT_EQUAL_UINT8(2, presets.count());
  TEST_ASSERT_EQUAL_STRING("Wiecz\xC3\xB3r", presets.at(0)->name);
  TEST_ASSERT_EQUAL_INT32(30, presets.at(0)->params[0]);
  TEST_ASSERT_EQUAL_UINT8(255, presets.find(2)->brightness);
  // nextId comes from the file: the next preset gets 3.
  const int32_t solid[] = {1};
  TEST_ASSERT_EQUAL_UINT8(3, presets.save(ledframe::makePreset("New", 0, solid, 1, 0, 1)));

  static PaintImage image;
  ArrayReader paintReader(reinterpret_cast<const char*>(kPaintBin), sizeof(kPaintBin));
  TEST_ASSERT_TRUE(image.deserialize(paintReader, sizeof(kPaintBin)));
  TEST_ASSERT_EQUAL_UINT16(3, image.width());
  TEST_ASSERT_EQUAL_UINT16(2, image.height());
  TEST_ASSERT_TRUE(image.at(1, 1) == CRGB(1, 2, 3));
  TEST_ASSERT_TRUE(image.at(0, 0) == CRGB(0, 0, 0));
}

// --- Files cut off in the middle (power loss outside the atomic write) --------------------

void test_truncated_json_gives_defaults() {
  Config config = sampleConfig();
  ArrayReader configReader(kConfigJson, std::strlen(kConfigJson) / 2);
  TEST_ASSERT_FALSE(ledframe::deserializeConfig(configReader, config));
  TEST_ASSERT_TRUE(config == Config{});

  State state = sampleState();
  ArrayReader stateReader(kStateJson, std::strlen(kStateJson) / 2);
  TEST_ASSERT_FALSE(ledframe::deserializeState(stateReader, registry(), state));
  expectDefaultState(state);

  static Presets presets;
  samplePresets(presets);
  ArrayReader presetsReader(kPresetsJson, std::strlen(kPresetsJson) / 2);
  TEST_ASSERT_FALSE(ledframe::deserializePresets(presetsReader, registry(), presets));
  TEST_ASSERT_EQUAL_UINT8(0, presets.count());

  // The buffer forms behave the same.
  config = sampleConfig();
  TEST_ASSERT_FALSE(ledframe::deserializeConfig(kConfigJson, std::strlen(kConfigJson) / 2, config));
  TEST_ASSERT_TRUE(config == Config{});
}

void test_truncated_paint() {
  static PaintImage image;
  samplePaint(image);
  const uint32_t version = image.version();

  // Cut in the header: the picture stays.
  ArrayReader header(reinterpret_cast<const char*>(kPaintBin), 5);
  TEST_ASSERT_FALSE(image.deserialize(header, sizeof(kPaintBin)));
  TEST_ASSERT_EQUAL_UINT16(3, image.width());
  TEST_ASSERT_EQUAL_UINT32(version, image.version());

  // The file is shorter than its header says: refused before any pixel is read.
  TEST_ASSERT_FALSE(image.deserialize(kPaintBin, sizeof(kPaintBin) - 3));
  TEST_ASSERT_EQUAL_UINT16(3, image.width());
  TEST_ASSERT_TRUE(image.at(1, 1) == CRGB(1, 2, 3));

  // The data ends early after a valid header: the picture is dropped.
  ArrayReader pixels(reinterpret_cast<const char*>(kPaintBin), sizeof(kPaintBin) - 3);
  TEST_ASSERT_FALSE(image.deserialize(pixels, sizeof(kPaintBin)));
  TEST_ASSERT_EQUAL_UINT16(0, image.size());
  TEST_ASSERT_TRUE(image.version() != version);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_config_bytes);
  RUN_TEST(test_state_bytes);
  RUN_TEST(test_presets_bytes);
  RUN_TEST(test_paint_bytes_in_parts);
  RUN_TEST(test_buffer_needs_room_for_terminator);
  RUN_TEST(test_write_error_is_reported);
  RUN_TEST(test_read_old_files);
  RUN_TEST(test_truncated_json_gives_defaults);
  RUN_TEST(test_truncated_paint);
  return UNITY_END();
}
