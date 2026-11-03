# Pomysły

Rzeczy, których jeszcze nie ma w firmware. Nic z tego nie jest zaplanowane ani sprawdzone na sprzęcie, chyba że opis mówi inaczej.

## Efekty

Matryca 15 × 10 ma tylko 150 pikseli, więc najlepiej wyglądają duże, miękkie kształty i płynne przejścia kolorów. Drobne detale giną.

Każdy z tych efektów wymaga czegoś więcej niż jednego pliku `.cpp` ([ADDING_EFFECT.md](ADDING_EFFECT.md)):

| `id` | Opis | Czego brakuje |
|---|---|---|
| `clock` | Godzina `HH:MM` czcionką 3 × 5: 4 cyfry po 3 kolumny, 2 kolumny odstępu i dwukropek to dokładnie 15 kolumn. Zostaje 5 wierszy na pasek sekund albo tło z palety. | Czas lokalny jest już w `FrameContext::wallTime`, więc sam zegar to jeden plik z czcionką bitmapową. Rysowanie zegara na innym efekcie wymagałoby warstwy nakładki w silniku. |
| `text` | Przewijany tekst. Przydałby się też w trybie AP albo po połączeniu, np. do pokazania adresu IP. | Nowy rodzaj parametru `ParamKind::Text` w stanie, API, UI i presetach, a w efekcie mała czcionka bitmapowa. |
| `sunrise` | Budzik świetlny: przez 10–30 min od czerni przez czerwień i pomarańcz do ciepłej bieli. | Harmonogram umie dziś tylko włączać i wyłączać. Budzik potrzebuje wpisu harmonogramu, który uruchamia efekt albo preset, i parametru czasu trwania. |
| `spectrum` | Analizator widma: 15 pasm × 10 wierszy, pasujące idealnie do tej matrycy. | Mikrofon I2S (np. INMP441), zadanie FFT na rdzeniu 0 i wejście audio w `FrameContext`. Wymaga zmian w sprzęcie. |
| `stream` | Wyświetlanie klatek z sieci (DDP albo E1.31), np. z xLights albo Hyperiona. | Ten sam wzorzec co `paint`: współdzielony bufor wypełniany z sieci i `EffectEngine::redraw()`, a do tego odbiornik UDP. |

Statyczny gradient nie potrzebuje osobnego efektu: daje go `rainbow` z prędkością 0 i małą gęstością. Wystarczy go zapisać jako preset.

## Funkcje

- **Edycja palety `custom`:** trzy kolory palety są zapisane w `/state.json` (`userColors`), ale API i UI nie pozwalają ich zmienić. Brakuje pola w `POST /api/state` i wyboru trzech kolorów w UI.
- **Rodzaje przejść:** dziś jest tylko przenikanie. Silnik ma przy przejściu oba płótna, więc wipe (próg zależny od `x`), dissolve (próg z hasza piksela) i przejście od środka to kilka linii. Rodzaj przejścia byłby nowym polem stanu obok `transition`.
- **Playlista:** zmiana presetu co N minut. Presety, przejścia i czas lokalny już są.
- **Harmonogram z presetami:** wpis harmonogramu, który oprócz `on` i `off` stosuje wybrany preset.
- **Serwer NTP w UI:** pole `ntpServer` jest dziś dostępne tylko przez `POST /api/config`.

## Wyjście na diody odporne na Wi-Fi

**Problem:**
- Przy ruchu Wi-Fi (otwarte UI z podglądem na żywo) pojedyncze diody na chwilę mają zły kolor.
- Przyczyna: sterownik RMT5 FastLED ma mały bufor sprzętowy. Uzupełnia go w przerwaniu poziomu 3, a przerwania Wi-Fi (poziom 4) go opóźniają.
- Flaga `FASTLED_RMT_MEM_BLOCKS_NETWORK_MODE=2` w `platformio.ini` podwoiła zapas do około 160 µs. Objaw jest rzadszy, ale nie zniknął. Więcej przez flagi się nie da, bo preset FastLED ustawia 2 bloki na stałe.

**Pomysł:** nadawać przez I2S (albo SPI) z DMA zamiast przez RMT.
- Każdy bit danych jest kodowany jako kilka bitów I2S (np. `1000` = 0, `1110` = 1), żeby czasy impulsów pasowały do WS2812B.
- Zakodowana klatka leży w RAM (przy 150 diodach około 2 KB), a DMA podaje ją do I2S bez udziału procesora.
- Jeśli cała klatka jest zakodowana przed wysłaniem, nie ma czego dopisywać w trakcie, więc przerwania Wi-Fi nie mogą zepsuć sygnału. Odporność wynika z konstrukcji, a nie z większego zapasu.

**Warianty:**

| Wariant | Odporność | Koszt zmiany |
|---|---|---|
| FastLED I2S z `FASTLED_ESP32_I2S_NUM_DMA_BUFFERS=4` | lepsza niż RMT, ale nie pełna: sterownik koduje dane małymi porcjami w przerwaniu (dokumentacja FastLED zaleca 4 bufory przy migotaniu od Wi-Fi) | flagi w `platformio.ini`, ewentualnie typ kontrolera w `LedOutput.cpp`; łatwo cofnąć |
| NeoPixelBus w trybie I2S (używa jej m.in. WLED) | pełna: cały bufor kodowany przed wysłaniem | nowa biblioteka, przepisany `LedOutput`; limit prądu, dithering, korekcję koloru taśmy i jasność, które dziś daje FastLED, trzeba przenieść albo napisać samemu |
| FastLED, sterownik SPI z DMA | jak wyżej, z założenia | FastLED sam oznacza go jako mniej niezawodny |

Nic z tego nie było sprawdzane na sprzęcie. To wiedza o bibliotekach i ich dokumentacji, więc decyzja wymaga pomiaru.

**Zakres:**
- Zmienia się tylko `render/LedOutput.cpp`. To cienka warstwa nad biblioteką, a efekty, silnik, API i UI jej nie widzą.
- RAM: około 2–8 KB na zakodowany bufor.
- ESP32 ma dwa układy I2S, więc mikrofon I2S dla `spectrum` dalej byłby możliwy na drugim.

**Proponowana kolejność:**
1. Wykluczyć sprzęt: konwerter poziomów 3,3 → 5 V na linii danych i inny pin niż GPIO 2, na którym wiele płytek ma własną diodę LED. Daje to podobny objaw, ale niezależny od Wi-Fi.
2. FastLED I2S z 4 buforami DMA. Ocenić błędy przy otwartym UI z podglądem i zmierzyć `showUsAvg` i FPS (`GET /api/info`).
3. Dopiero gdy to nie wystarczy, NeoPixelBus.
