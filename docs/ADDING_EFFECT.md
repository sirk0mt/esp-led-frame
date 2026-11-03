# Jak dodać efekt

Nowy efekt to **jeden plik `.cpp`** w `src/effects/builtin/` i, zalecany, jeden test. Efekt rejestruje się sam. Po kompilacji pojawia się w API (`GET /api/effects`), w UI (kafelek i panel ustawień), w zapisie stanu i w presetach. Nie zmieniasz silnika, API, frontendu ani żadnej listy.

## Spis treści

1. [Pełny przykład](#1-pełny-przykład)
2. [Parametry: `ParamSpec`](#2-parametry-paramspec)
3. [Rejestracja i flagi](#3-rejestracja-i-flagi)
4. [Cykl życia i `FrameContext`](#4-cykl-życia-i-framecontext)
5. [Rysowanie: płótno, palety, losowość](#5-rysowanie-płótno-palety-losowość)
6. [Zasady](#6-zasady)
7. [Widżety UI](#7-widżety-ui)
8. [Testy](#8-testy)
9. [Sprawdzenie na urządzeniu](#9-sprawdzenie-na-urządzeniu)
10. [Lista kontrolna](#10-lista-kontrolna)

## 1. Pełny przykład

Belka przesuwająca się po matrycy w przód i w tył, w kolorze z palety, z opcjonalnym zanikającym śladem. Kod jest sprawdzony: kompiluje się i przechodzi test z rozdziału 8. Pokazuje wszystkie rodzaje parametrów oprócz `Color`, który opisuje tabela w rozdziale 2.

`src/effects/builtin/ScannerEffect.cpp`:

```cpp
// Bar that sweeps across the matrix and back, colored from a palette.
//
//   speed      passes forth and back per minute
//   width      bar width in pixels
//   direction  horizontal: the bar is a column moving along x; vertical: a row
//   trail      on: the bar leaves a fading trail; off: only the bar is lit
//   palette    bar color, taken at the bar position
#include "effects/EffectRegistry.h"

namespace {

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::FrameContext;
using ledframe::ParamKind;
using ledframe::ParamSpec;

enum P : uint8_t { kSpeed, kWidth, kDirection, kTrail, kPalette };
enum Direction : int32_t { kHorizontal, kVertical };

constexpr ParamSpec kParams[] = {
    {"speed", "Speed", ParamKind::Slider, 1, 100, 20},
    {"width", "Width", ParamKind::Slider, 1, 8, 2},
    {"direction", "Direction", ParamKind::Select, 0, 0, kHorizontal, "horizontal|vertical"},
    {"trail", "Trail", ParamKind::Toggle, 0, 1, 1},
    {"palette", "Palette", ParamKind::Palette, 0, 0, 0},
};

constexpr uint32_t kTrailMs = 400;  // a full pixel fades out in about this time

class ScannerEffect : public Effect {
 public:
  bool render(FrameContext& ctx) override {
    Canvas& canvas = ctx.canvas;
    const bool vertical = ctx.params[kDirection] == kVertical;
    const int length = vertical ? canvas.height() : canvas.width();
    const int across = vertical ? canvas.width() : canvas.height();

    // Position from the clock, not from a frame counter: the same speed at any FPS.
    const uint32_t periodMs = 60000u / static_cast<uint32_t>(ctx.params[kSpeed]);
    const uint32_t half = periodMs / 2;
    const uint32_t phase = ctx.nowMs % periodMs;
    const uint32_t along = phase < half ? phase : periodMs - phase;  // 0 .. half .. 0
    const int position = static_cast<int>(along * static_cast<uint32_t>(length - 1) / half);

    if (ctx.params[kTrail] != 0) {
      const uint32_t amount = ctx.deltaMs * 255u / kTrailMs;  // proportional to the time
      canvas.fadeToBlackBy(static_cast<uint8_t>(amount > 255u ? 255u : amount));
    } else {
      canvas.clear();
    }

    const uint8_t index = static_cast<uint8_t>(length > 1 ? position * 255 / (length - 1) : 0);
    const CRGB color = ColorFromPalette(ctx.palette, index, 255, LINEARBLEND);
    for (int i = position; i < position + ctx.params[kWidth] && i < length; ++i) {
      for (int j = 0; j < across; ++j) {
        canvas.at(vertical ? j : i, vertical ? i : j) = color;
      }
    }
    return true;
  }
};

}  // namespace

REGISTER_EFFECT(ScannerEffect, "scanner", "Scanner", 90, ledframe::kFlag2D | ledframe::kFlagPalette,
                kParams);
```

Co tu jest ważne:

- **Struktura pliku:** wszystko w anonimowej przestrzeni nazw, a `REGISTER_EFFECT` poza nią, na końcu pliku.
- **Indeksy parametrów:** `enum P` daje nazwy pozycjom w `kParams`. `ctx.params[kSpeed]` to wartość parametru `speed`.
- **Zależność od czasu:** położenie wynika z `ctx.nowMs`, a zanikanie z `ctx.deltaMs`. Efekt wygląda tak samo przy 30 i przy 120 FPS.
- **Płótno między klatkami:** zostaje bez zmian, więc ślad powstaje przez przyciemnianie poprzedniej klatki.

## 2. Parametry: `ParamSpec`

```cpp
struct ParamSpec {
  const char* id;     // stały klucz w API, w pliku stanu i w presetach
  const char* label;  // etykieta w UI
  ParamKind kind;
  int32_t min, max, def;
  const char* options = nullptr;  // tylko Select: "a|b|c"
};
```

| `kind` | `min`, `max` | `def` | Kontrolka w UI | Wartość w `ctx.params` |
|---|---|---|---|---|
| `Slider` | zakres | w zakresie | suwak z liczbą | liczba z zakresu |
| `Toggle` | 0, 1 | 0 albo 1 | przełącznik | 0 albo 1 |
| `Color` | 0, `0xFFFFFF` | `0xRRGGBB` | wybór koloru | `0xRRGGBB`; `CRGB(static_cast<uint32_t>(v))` |
| `Select` | 0, 0 | indeks opcji | lista | indeks opcji od 0 |
| `Palette` | 0, 0 | indeks palety | lista palet z podglądem | indeks palety; kolorów nie czytasz z niego, tylko z `ctx.palette` |

Zasady:

- **`id`** składa się z małych liter, cyfr, `_` i `-`, np. `max_step`, nie `maxStep`. Rejestr odrzuca inne id, a błąd widać w logu startu. Nie zmieniaj `id` raz wydanego parametru: zapisany stan i presety szukają wartości po `id`. Zmiana `label` jest bezpieczna.
- **Najwyżej 16 parametrów** (`kMaxParams`) na efekt.
- **Zawsze poprawne wartości:** silnik przycina wartość do zakresu ze schematu, więc efekt nie musi jej sprawdzać. Dzielenie przez parametr jest bezpieczne, gdy `min` jest co najmniej 1.
- **Pamięć ustawień:** każdy efekt pamięta swoje wartości osobno. Powrót do efektu przywraca jego ustawienia.
- **Jednostki** opisz w komentarzu na początku pliku (jak w przykładzie): „passes per minute”, „ms”, „pixels”. UI pokazuje tylko liczbę.
- **Parametr `Palette`:** efekt z flagą `kFlagPalette` powinien mieć jeden taki parametr. Wtedy każdy efekt pamięta własną paletę, a wybór w UI pokazuje się jako jego ustawienie. `def` to indeks palety domyślnej:

  | Indeks | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|---|---|---|
  | Paleta | `rainbow` | `party` | `ocean` | `forest` | `lava` | `heat` | `cloud` | `custom` |

  Kolejność jest w `render/Palettes.cpp`. Test może sprawdzić indeks przez `Palettes::indexOf("heat")`.

## 3. Rejestracja i flagi

```cpp
REGISTER_EFFECT(Class, "id", "Name", order, flags, kParams);
REGISTER_EFFECT_WITH_WIDGET(Class, "id", "Name", order, flags, kParams, "widget");
```

- **`id`:** stały identyfikator efektu w API i w zapisie, według tych samych zasad co `id` parametru. Musi być unikalny.
- **`Name`:** nazwa na kafelku.
- **`order`:** kolejność kafelków, rosnąco. Obecne efekty to `solid` 10, `candle` 12, `breathe` 15, `rainbow` 50, `plasma` 52, `metaballs` 54, `radial-rainbow` 55, `swirl` 56, `noise` 57, `ripples` 58, `galaxy` 60, `twinkle` 65, `starfield` 67, `color-flow` 70, `rain` 72, `fire` 75, `paint` 80. Wybierz wolną liczbę.
- **`kParams`:** tablica `ParamSpec` albo `nullptr` dla efektu bez parametrów.
- **Klasa** musi mieć konstruktor domyślny i nazwę bez przestrzeni nazw.
- **Rozmiar obiektu:** silnik trzyma efekty w dwóch stałych slotach po `kEffectSlotSize` bajtów (`effects/Effect.h`), a nie na stercie, więc utworzenie efektu nigdy nie alokuje pamięci i nie może się nie udać z jej braku. Slot ma 2 B na diodę i 64 B zapasu, czyli 2112 B przy `MAX_LEDS = 1024`. Najwięcej zajmuje `radial-rainbow`: `uint16_t` na diodę. Za duża klasa nie kompiluje się, a `static_assert` z `REGISTER_EFFECT` mówi, co zrobić.
  - Mieść się w 2 B na diodę, np. `uint8_t` zamiast `uint16_t` albo jedna tablica zamiast dwóch.
  - Jeśli efekt naprawdę potrzebuje więcej, podnieś `kEffectSlotSize`. Każdy bajt slotu kosztuje 2 B RAM, bo slotów są dwa, nawet gdy efekt nie jest aktywny.

| Flaga | Znaczenie |
|---|---|
| `kFlag2D` | efekt korzysta z x i y. Na pasku 1D wygląda inaczej |
| `kFlagPalette` | efekt używa palety (`ctx.palette`) |
| `kFlagStatic` | efekt rysuje tylko po zmianie parametru. Silnik nie woła `render()` w każdej klatce, co oszczędza czas i nie wysyła klatek do diod (np. `solid`) |
| `kFlagHidden` | efekt nie pojawia się w API ani w UI (np. `status`, czerwony trójkąt w trybie AP) |

Flagi łączysz operatorem `|`, a efekt bez flag ma `0`. Problemy z rejestracją (zły deskryptor, powtórzone `id`, ponad 32 efekty) są zapisywane i logowane przy starcie. Test sprawdza je przez `EffectRegistry::global().ok()`.

## 4. Cykl życia i `FrameContext`

```cpp
class Effect {
 public:
  virtual ~Effect() = default;
  virtual void begin(FrameContext& ctx) {}                         // raz, po utworzeniu
  virtual bool render(FrameContext& ctx) = 0;                      // każda klatka
  virtual void onParamChanged(uint8_t index, FrameContext& ctx) {}  // po zmianie parametru
};

struct FrameContext {
  Canvas& canvas;               // płótno efektu
  uint32_t nowMs;               // czas od startu urządzenia
  uint32_t deltaMs;             // czas od poprzedniej klatki; 0 w pierwszej
  const int32_t* params;        // wartości w kolejności kParams
  const CRGBPalette16& palette; // paleta efektu
  bool gamma;                   // korekcja gamma kolorów użytkownika (ustawienie)
  WallTime wallTime;            // czas lokalny z NTP (core/WallTime.h), co najwyżej co 1 s
  CRGB userColor(int32_t rgb) const;        // kolor z parametru Color, gotowy dla diod
  CRGB userColor(const CRGB& color) const;  // to samo dla koloru CRGB
};
```

- **Kolory użytkownika:** kolor wybrany na ekranie (parametr `Color`, piksel narysowany w `paint`) rysuj zawsze przez `ctx.userColor()`, np. `ctx.canvas.fill(ctx.userColor(ctx.params[kColor]))`. Przy włączonym ustawieniu „Color correction” pomocnik stosuje gamma 2,2, a przy wyłączonym oddaje kolor bez zmian. Nie licz korekcji sam. Kolory z `ctx.palette` są już gotowe dla diod, także paleta `custom`, którą silnik koryguje sam.

- **Czas lokalny (`ctx.wallTime`):** `known` mówi, czy zegar był zsynchronizowany z NTP. Do tego czasu, w trybie AP i w testach (o ile test go nie ustawi) jest `false`, a pozostałe pola mają 0. Pola: `year`, `month` (1–12), `day`, `hour`, `minute`, `second`, `weekday` (0 = poniedziałek … 6 = niedziela) i `minuteOfDay()`. App odświeża czas raz na sekundę, więc `second` zmienia się skokowo. Do płynnej animacji dalej służą `nowMs` i `deltaMs`. Efekt zależny od godziny ustawia czas w teście przez `FrameContext ctx{...}; ctx.wallTime = time;`.

- **Tworzenie instancji:** instancja istnieje tylko wtedy, gdy efekt jest aktywny albo bierze udział w przejściu. Po wybraniu innego efektu jest usuwana razem z polami klasy. Stan, który ma przetrwać zmianę efektu, nie może więc żyć w polach klasy. Przykład: obraz `paint` jest w `sharedPaintImage()`.
- **`begin()`:** wołane raz po utworzeniu, na wyczyszczonym płótnie. Tu przygotuj dane zależne od rozmiaru albo parametrów, np. tablicę odległości od środka. Po zmianie układu matrycy silnik tworzy efekt od nowa, więc `begin()` widzi nowy rozmiar.
- **`render()`:** rysuje klatkę i zwraca `true`, gdy obraz się zmienił. `false` (nic się nie zmieniło) pozwala pominąć wysłanie klatki do diod, np. `rainbow` z prędkością 0.
- **`onParamChanged(index, ctx)`:** wołane po zmianie wartości parametru `index`, przed następnym `render()`. Tu przelicz to, co zależy od parametru (np. wektor kierunku z kąta w `rainbow`), zamiast liczyć to w każdej klatce.
- **Płótno między klatkami:** należy do efektu i nie jest czyszczone, więc efekt może budować na poprzedniej klatce (`fadeToBlackBy`). Efekt, który rysuje wszystko od nowa, zaczyna od `canvas.clear()` albo zapisuje każdy piksel.
- **Przejścia:** silnik sam miesza stary i nowy efekt, a efekt nie musi o tym wiedzieć.

## 5. Rysowanie: płótno, palety, losowość

**Płótno (`render/Canvas.h`):**

| Metoda | Działanie |
|---|---|
| `width()`, `height()`, `size()` | wymiary matrycy w układzie logicznym |
| `at(x, y)` | piksel (`CRGB&`). Poza zakresem daje piksel zastępczy, a zapis do niego przepada |
| `canvas[i]`, `data()` | piksel `i = y * width + x` i surowa tablica. `data()` jest najszybsze, ale nie sprawdza zakresu |
| `fill(color)`, `clear()` | wypełnienie kolorem albo czernią |
| `fadeToBlackBy(amount)` | przyciemnienie każdego piksela o `amount/256` |

- **Układ logiczny:** (0, 0) to lewy górny róg, x rośnie w prawo, a y w dół. Wężyk, narożnik startowy, obrót, odbicia i kolejność kolorów obsługuje warstwa wyjścia. Efekt rysuje zawsze w układzie logicznym i w RGB.
- **Jasność i limit prądu** stosuje warstwa wyjścia. Efekt rysuje w pełnej skali 0–255.

**Palety:**

```cpp
const CRGB color = ColorFromPalette(ctx.palette, index /* 0..255 */, 255 /* jasność */, LINEARBLEND);
```

Dostępne palety to `rainbow`, `party`, `ocean`, `forest`, `lava`, `heat`, `cloud` i `custom`. Wybiera je użytkownik, a efekt dostaje gotową paletę w `ctx.palette`.

**Indeks jako siła, nie barwa:** `LINEARBLEND` traktuje paletę jak koło, więc indeksy 241–255 mieszają ostatni kolor z pierwszym. Efekt, w którym indeks oznacza natężenie (pole `metaballs`, wysokość fali `ripples`) i bywa obcinany do 255, używa `LINEARBLEND_NOWRAP`. Inaczej najjaśniejsze miejsca ciemnieją do pierwszego koloru palety. `fire` omija to inaczej, mnożąc indeks przez 240/255.

**Losowa barwa bez parametru `Palette`:** `ColorFromPalette(RainbowColors_p, hue, 255, LINEARBLEND)` daje barwę tęczy gotową dla diod, bez korekcji jak kolor użytkownika (`rain` i `starfield` z `colors = random`). `RainbowColors_p` wymaga `#include <colorpalettes.h>` **po** `#include "effects/EffectRegistry.h"`, bo nagłówek potrzebuje typów FastLED (`CHSV`), a postawiony pierwszy daje w testach natywnych błędy w `fl/gfx/fill.h`. Konwersji `CHSV` → `CRGB` nie używaj, bo nie wiadomo, czy linkuje się w testach.

**Funkcje FastLED:** można używać typów i funkcji matematycznych (`CRGB`, `CHSV`, `ColorFromPalette`, `scale8`, `qadd8`, `qsub8`, `sin8`, `beatsin8`, `nblend`, `random8`, `random16`). Nie używaj obiektu `FastLED` ani funkcji, które piszą do diod.

**Losowość:**

- **Bez własnego źródła:** `random8()` i `random16()` z FastLED mają ziarno ustawiane przy starcie z `esp_random()`.
- **Własny generator:** gdy efekt potrzebuje lepszej losowości, może mieć własny, z ziarnem pobranym w `begin()` z `random16()`. Tak robi `galaxy`.
- **Powtarzalne testy:** test ustawia ziarno przez `random16_set_seed(1234)` przed utworzeniem efektu (`#include <lib8tion.h>`).
- **Symulacja krok po kroku i FPS:** efekt, który z natury liczy kolejne kroki (np. `fire`), może je wykonywać ze stałą częstotliwością. Zbiera `deltaMs` i wykonuje krok co `1000 / kroki_na_sekundę` ms, a liczbę kroków w jednej klatce ogranicza na wypadek przestoju. Klatka bez kroku zwraca `false`. Przy stałym ziarnie ten sam czas daje wtedy identyczny obraz przy każdym FPS, co łatwo sprawdzić testem.
- **Płynny ruch przy rzadkich krokach:** gdy krok przesuwa coś o cały piksel (np. `rain` przy kilku wierszach na sekundę), ruch skacze. Lepiej trzymać w krokach tylko stan i losowanie, a obraz rysować w każdej klatce z położenia w ułamkach piksela: `faza = reszta_ms * 256 / długość_kroku`. Nowy piksel się wtedy rozjaśnia, a stary gaśnie płynnie, a ten sam czas dalej daje ten sam obraz przy każdym FPS. Wzorem jest `rain`.
- **Płynnie dla oka, nie tylko w liczbach:** oko widzi jasność diody mniej więcej jak pierwiastek z PWM, więc liniowe w PWM rozjaśnianie wygląda jak skok, a potem postój. Przejścia (rozjaśnianie, gaśnięcie, wagi antyaliasingu) licz w jasności postrzeganej i dopiero na końcu podnieś ją do kwadratu. Najciemniejsze poziomy (0, 1, 2) skaczą, a dithering FastLED nie działa przy jasności 255, więc licz w 16 bitach i dodaj szum 0–255 z hasza czasu i piksela przed obcięciem do 8 bitów. Wzorem są `rain` i `starfield`.

## 6. Zasady

- **Bez alokacji:** żadnego `new`, `std::vector`, `String`, ani w `render()`, ani w `begin()`. Bufory to tablice o stałym rozmiarze w polach klasy (np. `uint8_t heat_[kMaxLeds]` w `fire`), w granicach slotu (rozdział 3). W `begin()` je wypełnij. Największy możliwy rozmiar to `kMaxLeds` (1024) pikseli.
- **Animacja z czasu:** z `nowMs` (położenie, faza) i `deltaMs` (przyrosty, zanikanie), nigdy z licznika klatek. Uważaj na przepełnienie: `nowMs * speed * 256` wykracza poza 32 bity po kilku godzinach, więc licz w 64 bitach albo modulo okres. Gdy wynik obcinasz do 8 albo 16 bitów (kąt `sin8`, współrzędna `inoise8`), wielokrotność obciętej wartości jest poprawna (`plasma`), ale jej część, np. połowa, skacze przy przepełnieniu. Taką wielkość licz z pełnej 64-bitowej wartości i dopiero ją obetnij (`noise`).
- **Bez Arduino i sieci:** efekt nie zna `Serial`, plików, Wi-Fi, `millis()` ani `delay()`. Wszystko dostaje w `FrameContext`. Dzięki temu kompiluje się na komputerze w testach.
- **Szybko:** cel to `render()` poniżej 2 ms dla 15 × 10. Obecne efekty potrzebują 0,1–0,3 ms. Kosztowne obliczenia (`sin`, `sqrt`, dzielenia na pikselach) przenieś do `begin()` albo `onParamChanged()`.
- **Kod po angielsku**, przestrzeń nazw `ledframe` (efekt i tak siedzi w anonimowej), formatowanie według `.clang-format`, bez nowych ostrzeżeń (`-Wall -Wextra`).
- **Logi:** jeśli potrzebne, tylko makrami `LOG_E/W/I/D` z `core/Log.h`, nigdy w `render()`.

## 7. Widżety UI

Panel ustawień powstaje sam ze schematu parametrów. Gdy efekt potrzebuje własnego komponentu, np. siatki do rysowania w `paint`, rejestruje go nazwą:

```cpp
REGISTER_EFFECT_WITH_WIDGET(PaintEffect, "paint", "Paint", 80, ledframe::kFlagStatic | ledframe::kFlag2D,
                            nullptr, "paint");
```

UI wczytuje wtedy moduł `web/ui/widgets/<nazwa>.js`, gdy użytkownik wybierze efekt. Moduł eksportuje:

```js
export function mount(container, ctx) {
  // container: element na widżet
  // ctx.effect      deskryptor z GET /api/effects
  // ctx.state       bieżący stan (format GET /api/state)
  // ctx.send(patch, now)    zmiana stanu, np. { params: { speed: 5 } }
  // ctx.sendBinary(bytes)   wiadomość binarna WebSocket; false bez połączenia
  // ctx.on("binary" | "open", fn)  wiadomości binarne i ponowne połączenia; zwraca funkcję wypisania
  // ctx.getJson(path), ctx.postJson(path, body)
  return {
    update(state) {},  // każdy nowy stan
    destroy() {},      // wybrano inny efekt
  };
}
```

Nazwa widżetu to małe litery, cyfry, `_` i `-`. Brak pliku nie psuje UI: zamiast widżetu pokazuje się komunikat, a parametry działają dalej.

Widżet z własnym protokołem potrzebuje też zmian w firmware. Przykład: binarna wiadomość `0x01` w `net/WsHub.cpp` dla `paint`. Pliki w `web/` są gzipowane do firmware przy kompilacji. Nie używaj w nich wielolinijkowych literałów szablonowych ani `<pre>`, bo skrypt usuwa wcięcia.

## 8. Testy

Testy działają na komputerze (PlatformIO `native`, Unity, FastLED w wersji bez sprzętu). Wszystkie efekty z `src/effects/builtin/` są w nich zarejestrowane, więc test znajduje efekt przez rejestr, tak jak firmware.

`test/test_scanner/test_main.cpp` (osobny katalog na efekt albo nowe testy dopisane do `test/test_effects/test_main.cpp`):

```cpp
#include <unity.h>

#include "effects/EffectRegistry.h"
#include "render/Palettes.h"

using ledframe::Canvas;
using ledframe::Effect;
using ledframe::EffectDescriptor;
using ledframe::EffectRegistry;
using ledframe::FrameContext;
using ledframe::Palettes;

namespace {

enum P : uint8_t { kSpeed, kWidth, kDirection, kTrail, kPalette };

Canvas canvas;
Palettes palettes;

// One scanner with its own parameter values: defaults, then the given changes. The
// EffectSlot holds the effect like the engine does and destroys it with the struct.
struct Scanner {
  Scanner() {
    descriptor = EffectRegistry::global().find("scanner");
    ledframe::loadDefaults(*descriptor, params);
    effect = slot.create(*descriptor);
  }

  bool render(uint32_t nowMs, uint32_t deltaMs = 20) {
    FrameContext ctx{canvas, nowMs, deltaMs, params, palettes.get(params[kPalette])};
    return effect->render(ctx);
  }

  const EffectDescriptor* descriptor = nullptr;
  ledframe::EffectSlot slot;
  int32_t params[ledframe::kMaxParams] = {};
  Effect* effect = nullptr;
};

int light(int x, int y) {
  const CRGB& c = canvas.at(x, y);
  return c.r + c.g + c.b;
}

bool lit(int x, int y) {
  return light(x, y) > 0;
}

}  // namespace

void setUp() {
  TEST_ASSERT_TRUE(canvas.resize(15, 10));
}
void tearDown() {}

void test_scanner_is_registered() {
  const EffectDescriptor* scanner = EffectRegistry::global().find("scanner");
  TEST_ASSERT_NOT_NULL(scanner);
  TEST_ASSERT_TRUE(EffectRegistry::global().ok());
  TEST_ASSERT_EQUAL_UINT8(5, scanner->paramCount);
  TEST_ASSERT_EQUAL_INT(kDirection, ledframe::findParam(*scanner, "direction"));
}

void test_bar_follows_the_clock() {
  Scanner scanner;
  scanner.params[kTrail] = 0;  // only the bar, so lit pixels show its position
  scanner.params[kWidth] = 1;
  scanner.params[kSpeed] = 60;  // one pass forth and back per second
  scanner.render(0);
  TEST_ASSERT_TRUE(lit(0, 0));
  TEST_ASSERT_TRUE(lit(0, 9));  // the whole column
  TEST_ASSERT_FALSE(lit(1, 0));
  scanner.render(500);  // half a period: the far end
  TEST_ASSERT_TRUE(lit(14, 5));
  TEST_ASSERT_FALSE(lit(13, 5));
  scanner.render(1000);  // back at the start
  TEST_ASSERT_TRUE(lit(0, 5));
}

void test_vertical_bar_is_a_row() {
  Scanner scanner;
  scanner.params[kTrail] = 0;
  scanner.params[kDirection] = 1;  // vertical
  scanner.render(0);
  TEST_ASSERT_TRUE(lit(0, 0));
  TEST_ASSERT_TRUE(lit(14, 0));  // the whole row
  TEST_ASSERT_FALSE(lit(0, 2));
}

void test_trail_fades_over_time() {
  Scanner scanner;
  scanner.params[kSpeed] = 60;
  scanner.params[kWidth] = 1;
  scanner.render(0);
  const int start = light(0, 0);
  scanner.render(100, 100);  // the bar moved on; column 0 fades
  const int later = light(0, 0);
  TEST_ASSERT_TRUE(later > 0);
  TEST_ASSERT_TRUE(later < start);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scanner_is_registered);
  RUN_TEST(test_bar_follows_the_clock);
  RUN_TEST(test_vertical_bar_is_a_row);
  RUN_TEST(test_trail_fades_over_time);
  return UNITY_END();
}
```

Struktura `Scanner` nie woła `begin()`, bo przykład go nie nadpisuje. Jeśli Twój efekt nadpisuje `begin()` (bufory, odległości, ziarno), wywołaj je w konstruktorze struktury testowej, tak jak robi to silnik: po `slot.create()`, na płótnie o docelowym rozmiarze. Po `canvas.resize()` utwórz efekt od nowa, bo silnik też tworzy efekt od nowa po zmianie układu. `slot.create()` sam niszczy poprzedni efekt w slocie:

```cpp
effect = slot.create(*descriptor);
FrameContext ctx{canvas, 0, 0, params, palettes.get(params[kPalette])};
effect->begin(ctx);
```

Co warto testować:

- **Rejestracja:** efekt jest w rejestrze, `ok()` zgłasza brak problemów, a parametry są na oczekiwanych pozycjach.
- **Powtarzalność:** ten sam czas i te same parametry dają ten sam obraz.
- **Kształt obrazu:** charakterystyczne cechy, np. jednakowe kolumny przy kącie 0 w `rainbow`.
- **Niezależność od FPS:** ten sam czas w klatkach co 10 ms i co 100 ms daje ten sam albo bardzo podobny wynik.
- **Wartości skrajne:** minimum i maksimum każdego parametru, matryca 1 × 1 i 1 × 1024.

Uwagi praktyczne:

- **Ciała w `.hpp`:** niektóre metody `CRGB` (np. `getAverageLight`, `subtractFromRGB`) mają ciało tylko w plikach `.hpp` FastLED i w testach nie linkują się. Licz wprost na `r`, `g` i `b` albo użyj `qsub8` i `scale8`.
- **Standard i platforma:** kod efektów i testów kompiluje się jako C++17. Platformę rozpoznawaj przez `ESP32` albo `LEDFRAME_NATIVE`, nie przez `ARDUINO`.

Uruchomienie:

```sh
pio test -e native -f test_scanner      # jeden zestaw, około 30 s
pio test -e native                      # wszystkie, kilka minut
pio run -e esp32dev                     # kompilacja firmware
```

Środowisko testów natywnych i jego ograniczenia opisuje [DEVELOPMENT.md](DEVELOPMENT.md#6-testy). Testy wymagają gcc w `PATH`. W PowerShell na komputerze autora: `$env:Path = "C:\mingw64\bin;" + $env:Path` przed `pio test`, a `pio` to `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`. Pierwsza kompilacja trwa kilka minut, głównie przez FastLED.

### Podgląd w konsoli bez sprzętu

`scripts/show_effect.py` pokazuje efekt w terminalu, zanim trafi na diody. Skrypt uruchamia zestaw `test/test_render_dump`, który renderuje efekt przez prawdziwy `EffectEngine` z fałszywym zegarem (domyślnie 15 × 10, 3 s, 25 FPS, stałe ziarno losowości). Pierwsze uruchomienie buduje test i trwa około minuty, kolejne kilka sekund. Sam znajduje `pio` i dodaje `C:\mingw64\bin` do `PATH`, jeśli nie ma tam gcc.

```sh
python scripts/show_effect.py scanner                          # 6 klatek w kolorze
python scripts/show_effect.py scanner:speed=120,width=3 --play --loop  # animacja
python scripts/show_effect.py fire radial-rainbow --mode ascii --frames 3
python scripts/show_effect.py                                  # wszystkie efekty z UI
```

- **Parametry:** `id:nazwa=wartość,...`, gdzie wartość to liczba, `0x…`, `#RRGGBB` albo nazwa opcji `Select` (np. `direction=inward`). Niepodane parametry mają wartości domyślne, a silnik przycina wartości jak w API.
- **`--mode color`** (domyślnie w terminalu): diody jako kolorowe bloki, zgaszona dioda jako szara kropka. `--play` odtwarza animację w miejscu, a `--loop` ją powtarza.
- **`--mode ascii`** (domyślnie, gdy wyjście nie jest terminalem): tekst bez kolorów, do oceny przez agenta albo w logu. Każda dioda to litera barwy (`R O Y G C B P`, `W` dla bieli i szarości) i cyfra jasności 0–9, a ` .` to dioda zgaszona. Na końcu wiersza jest jego średnia jasność, co pokazuje np. gradient ognia od dołu.
- **Inne opcje:** `--ms`, `--fps`, `--size 16x16`, `--frames N` (ile klatek wypisać, równo rozłożonych w czasie), `--gamma` (kolory użytkownika z korekcją), `--keep plik` (zachowaj surowe klatki) i `--dump plik` (pokaż zapisane klatki bez renderowania).
- **Bez skryptu:** zmienne `LEDFRAME_DUMP` (plik wynikowy), `LEDFRAME_DUMP_EFFECTS` (specyfikacje oddzielone `;`), `LEDFRAME_DUMP_MS`, `_FPS`, `_SIZE` i `_GAMMA`, a potem `pio test -e native -f test_render_dump`. Format pliku jest opisany na początku `test/test_render_dump/test_main.cpp`. Bez `LEDFRAME_DUMP` zestaw nic nie robi, więc zwykłe `pio test` i CI go pomijają.

## 9. Sprawdzenie na urządzeniu

1. Zbuduj firmware: `pio run -e esp32dev`. Sprawdź, czy kompilacja nie dała nowych ostrzeżeń z plików w `src/`.
2. Wgraj je przez sieć: `curl -F firmware=@.pio/build/esp32dev/firmware.bin http://ledframe.local/api/ota` (w PowerShell `curl.exe`) albo w UI: Device → Firmware.
3. Sprawdź `GET /api/effects`: efekt ma się pojawić z parametrami. W UI ma być kafelek i panel ustawień.
4. Przełącz efekt i zmień parametry. W pasku informacji na dole strony (rozwiń go kliknięciem) albo w `GET /api/info` sprawdź `renderUsAvg` i `renderUsMax` (cel poniżej 2000 µs) i stabilne FPS.
5. Przełącz na inny efekt i z powrotem: ustawienia mają zostać.

## 10. Lista kontrolna

- [ ] Jeden nowy plik `src/effects/builtin/<Nazwa>Effect.cpp`, zakończony `REGISTER_EFFECT`.
- [ ] `id` efektu i parametrów: małe litery, cyfry, `_`, `-`, unikalne.
- [ ] Jednostki i znaczenie parametrów w komentarzu na początku pliku.
- [ ] Animacja liczona z `nowMs` i `deltaMs`, bez liczników klatek i bez przepełnień.
- [ ] Brak alokacji w `render()` i brak Arduino, `millis()`, `delay()`, `Serial`.
- [ ] Kosztowne obliczenia w `begin()` lub `onParamChanged()`.
- [ ] `render()` zwraca `false`, gdy obraz się nie zmienił, jeśli to możliwe.
- [ ] Test w `test/`, `pio test -e native` przechodzi.
- [ ] Wygląd obejrzany przez `scripts/show_effect.py` (kolor albo `--mode ascii`).
- [ ] `pio run -e esp32dev` bez nowych ostrzeżeń.
- [ ] Na urządzeniu: kafelek, parametry i czas renderowania poniżej 2 ms.
