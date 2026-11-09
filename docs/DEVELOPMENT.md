# Rozwój projektu

Dokument dla osób, które zmieniają firmware albo interfejs WWW. Obsługę urządzenia opisuje [README](../README.md), API [API.md](API.md), a pisanie efektów [ADDING_EFFECT.md](ADDING_EFFECT.md).

## Spis treści

1. [Struktura repozytorium](#1-struktura-repozytorium)
2. [Architektura firmware](#2-architektura-firmware)
3. [Interfejs WWW](#3-interfejs-www)
4. [Pamięć flash i pliki](#4-pamięć-flash-i-pliki)
5. [Środowisko i kompilacja](#5-środowisko-i-kompilacja)
6. [Testy](#6-testy)
7. [CI](#7-ci)
8. [Zasady kodu](#8-zasady-kodu)
9. [Wersje](#9-wersje)

## 1. Struktura repozytorium

| Ścieżka | Zawartość |
|---|---|
| `src/core/` | wspólne drobiazgi: limity kompilacji (`Limits.h`), wersja (`Version.h`), logi (`Log.h`), strumienie bajtów, czas lokalny |
| `src/render/` | płótno, układ matrycy, palety, korekcja gamma, wyjście na diody |
| `src/effects/` | interfejs efektu, rejestr, silnik efektów, obraz `paint` |
| `src/effects/builtin/` | efekty, po jednym pliku na efekt |
| `src/state/` | stan, konfiguracja, presety, harmonogram i zapis plików |
| `src/net/` | Wi-Fi, serwer HTTP i API, WebSocket, OTA, DNS captive portalu |
| `src/app/` | aplikacja i zadanie renderowania, konsola szeregowa, raport awarii |
| `src/main.cpp` | `setup()` i `loop()` Arduino, które wołają `App` |
| `src/generated/` | pliki generowane przy kompilacji (interfejs WWW), poza repozytorium |
| `web/` | interfejs WWW: HTML, CSS i moduły ES |
| `test/` | testy natywne Unity, po jednym katalogu na zestaw |
| `scripts/` | `embed_web.py` (wbudowanie `web/`), `native_env.py` (poprawki środowiska testów), `show_effect.py` (podgląd efektów w terminalu), `release_notes.sh` (opis wydania w CI) |
| `platformio.ini` | środowiska, wersje platformy i bibliotek, flagi kompilacji |
| `partitions.csv` | tablica partycji flash |
| `.github/workflows/ci.yml` | CI |

Każdy plik nagłówkowy zaczyna się komentarzem, który opisuje zadanie modułu i jego zasady. To pierwsze miejsce, do którego warto zajrzeć przed zmianą.

## 2. Architektura firmware

### Moduły

| Moduł | Zadanie |
|---|---|
| `App` | tworzy moduły, uruchamia zadanie renderowania, przyjmuje komendy z innych zadań, zapisuje pliki, obsługuje konsolę i harmonogram, zatwierdza firmware po aktualizacji (`RollbackGuard`) |
| `EffectEngine` | aktywny efekt, parametry wszystkich efektów, przejścia, harmonogram klatek, statystyki. Nie zna FreeRTOS ani sprzętu |
| `EffectRegistry` | lista efektów, które rejestrują się same makrem `REGISTER_EFFECT` |
| `Canvas`, `Layout` | płótno logiczne (wiersz po wierszu od lewego górnego rogu) i odwzorowanie na fizyczną kolejność diod |
| `OutputMapping`, `LedOutput` | kolejność kolorów, układ, jasność, limit prądu i wysłanie klatki przez FastLED |
| `Palettes`, `Gamma` | palety i korekcja kolorów wybranych na ekranie |
| `State`, `Config`, `Presets`, `Schedule` | dane zapisywane w plikach, z serializacją JSON jako czystymi funkcjami |
| `Storage` | zapis i odczyt plików LittleFS: atomowo, z opóźnieniem, strumieniowo |
| `WifiManager` | stacja z limitem czasu, tryb AP z DNS captive portalu, ponowne łączenie, mDNS, skan. Serwery DNS IPv6 z ogłoszeń routera (RDNSS), nieosiągalne bez adresu IPv6, zastępuje serwerami IPv4 z DHCP |
| `WebServer`, `ApiJson` | trasy HTTP i parsowanie oraz budowanie JSON API |
| `WsHub`, `WsProtocol` | WebSocket `/ws`: rozgłaszanie stanu, wiadomości `paint` i podglądu |
| `Ota` | `POST /api/ota` i ArduinoOTA |
| `CrashReport` | przyczyna restartu i podsumowanie coredumpu |

### Zadania i przepływ danych

- **Zadanie `render`** (rdzeń 1, priorytet 3) jest jedynym właścicielem `EffectEngine` i `LedOutput`. Nic innego ich nie dotyka.
- **Pozostałe zadania** wysyłają komendy (`effects/Command.h`) przez kolejkę FreeRTOS: pętla Arduino (konsola, zapis plików, `WifiManager::tick()` co 10 ms) i zadanie AsyncTCP (obsługa HTTP i WebSocket). Pełna kolejka daje w API `503`.
- **Komendy są stosowane między klatkami**, nigdy w trakcie rysowania. Komenda budzi zadanie od razu, więc nie czeka na następną klatkę.
- **Termin klatki ustala tylko silnik** z dokładnością do mikrosekund, więc 60 FPS to dokładnie 60 klatek na sekundę. Po przestoju silnik zaczyna od bieżącej chwili, bez serii zaległych klatek.
- **Zapis do flash czeka na przerwę między klatkami.** W czasie operacji na flash cache procesora jest wyłączony na obu rdzeniach. Przerwanie RMT, które dosyła dane do diod, nie jest w IRAM (`CONFIG_RMT_TX_ISR_CACHE_SAFE` jest wyłączone w frameworku), więc czeka razem z nim. Klatka wysyłana w tym czasie dochodzi uszkodzona: RMT powtarza stare dane, w pomiarze nawet 28 000 bitów zamiast 3600. Dlatego każdy zapis plików i stanu firmware (`App::FlashWrite`) zatrzymuje zadanie `render` między dwiema klatkami (`FastLED.show()` wraca dopiero po wysłaniu klatki) i wznawia je po zapisie. Animacja stoi wtedy przez 3–4 klatki zamiast pokazać uszkodzoną. Nowy kod, który pisze do flash (LittleFS, NVS, `esp_ota_*`) poza aktualizacją firmware, musi robić to w `FlashWrite`.
- **Migawka stanu** (`StateSnapshot`) jest publikowana przez zadanie `render` pod muteksem. API i WebSocket czytają kopię przez `App::snapshot()`. Jej `version` rośnie przy każdej zmianie, a `WsHub` rozgłasza nowy stan najwyżej co 50 ms.

### Droga klatki

1. Efekt rysuje na własnym płótnie w układzie logicznym i w RGB, w pełnej skali 0–255. Kolory wybrane przez użytkownika przechodzą przez `ctx.userColor()` (korekcja gamma, gdy jest włączona).
2. W czasie przejścia silnik miesza płótna starego i nowego efektu na trzecim płótnie.
3. Warstwa wyjścia odwzorowuje piksele na fizyczne diody (narożnik, obrót, wężyk, odbicia) i zamienia kolejność kanałów.
4. FastLED stosuje jasność, limit prądu i dithering i wysyła klatkę. Przy 150 diodach wysłanie trwa około 7 ms, dlatego FPS jest ograniczone do 120.
5. Efekt statyczny (`kFlagStatic`) rysuje tylko po zmianie, a klatka bez zmian nie jest wysyłana.

### Pamięć

- **Bez alokacji w drodze renderowania.** Efekty żyją w dwóch stałych slotach (`EffectSlot`, 2 B na diodę i 64 B zapasu), a płótna mają stały rozmiar dla `MAX_LEDS`.
- **Odpowiedzi JSON** są budowane w buforze o rozmiarze tej odpowiedzi, a nie w stałym dużym buforze. Nie używaj `String` do budowania odpowiedzi.
- **Pliki** są zapisywane i czytane strumieniowo przez bufor 256 B, więc żaden plik nie leży w RAM w całości. Największy plik to 32 KB (32 presety z 16 parametrami to około 18 KB).
- **Jeden bufor podglądu** na żywo, kopiowany tylko wtedy, gdy ktoś go subskrybuje.

## 3. Interfejs WWW

| Plik | Zadanie |
|---|---|
| `web/index.html`, `web/style.css` | strona główna i style, z trybem ciemnym przez `prefers-color-scheme` |
| `web/app.js` | punkt wejścia: górny pasek, karty, kafelki, panel parametrów, synchronizacja stanu |
| `web/api.js` | REST i WebSocket z ponownym łączeniem i `ping` co 10 s |
| `web/ui/controls.js` | funkcje DOM, kafelki efektów, panel parametrów, krzywa suwaka jasności |
| `web/ui/presets.js` | karta Presets |
| `web/ui/settings.js` | karta Device |
| `web/ui/status.js` | pasek informacji na dole strony |
| `web/ui/preview.js` | podgląd na żywo, wczytywany dopiero po włączeniu |
| `web/ui/widgets.js`, `web/ui/widgets/paint.js` | ładowanie widżetów efektów i widżet `paint` |
| `web/setup.html` | strona konfiguracji Wi-Fi w trybie AP |

**Zasady:**

- **Bez frameworka i kroku budowania:** czyste moduły ES, bez zasobów z internetu (urządzenie często działa bez niego). Teksty UI są po angielsku, jak nazwy efektów i parametrów w firmware.
- **Mało plików naraz:** AsyncTCP ma na stałe kolejkę 5 połączeń, a serwer zamyka połączenie po każdej odpowiedzi. Przy kilku plikach pobieranych równolegle nadmiarowe połączenia są odrzucane, a przeglądarka ponawia je po około sekundzie. Dlatego `app.js` statycznie importuje tylko `api.js` i `ui/controls.js`, a resztę wczytuje przez `import()` przy pierwszym użyciu. Nowy moduł dodawaj tak samo.
- **Panel parametrów** powstaje ze schematu `GET /api/effects`, więc nowy efekt nie wymaga zmian w `web/`.
- **Wysyłanie zmian:** zmiany suwaków są łączone i wysyłane najwyżej co 50 ms, a kliknięcia od razu. Kontrolka zmieniona przez użytkownika ignoruje przez chwilę nadchodzący stan, żeby starsze rozgłoszenie nie cofało suwaka.

**Wbudowanie w firmware** (`scripts/embed_web.py`, uruchamiany przed każdą kompilacją ESP32):

- Każdy plik z `web/` (bez plików zaczynających się kropką) trafia do `src/generated/web_assets.h` jako tablica w flash, z typem MIME i ETag (SHA-256 treści).
- Z plików JS, CSS i HTML skrypt usuwa wcięcia i komentarze w osobnych liniach, zamienia CRLF na LF i kompresuje gzipem. Ten sam plik daje zawsze te same bajty, także w CI na Linuksie.
- Skoro wcięcia znikają, w `web/` nie używaj wielolinijkowych literałów szablonowych ani `<pre>`.
- Nieznane rozszerzenie albo ścieżka ze znakami spoza `A-Za-z0-9._/-` zatrzymuje kompilację. Nowy typ pliku dopisz do `MIME_TYPES` w skrypcie.
- Skrypt wypisuje łączny rozmiar po kompresji i ostrzega powyżej 25 KB.
- `src/generated/` ma własny `.gitignore` tworzony przez skrypt, bo wtyczka PlatformIO w VS Code nadpisuje główny `.gitignore` swoim szablonem.
- Ręcznie: `python scripts/embed_web.py` z katalogu projektu.

## 4. Pamięć flash i pliki

`partitions.csv` dla płytki z 4 MB flash:

| Partycja | Rozmiar | Zawartość |
|---|---|---|
| `nvs` | 20 KB | dane systemowe ESP32 |
| `otadata` | 8 KB | która partycja aplikacji jest aktywna |
| `app0`, `app1` | po 1792 KB | dwie partycje firmware: działająca i ta, do której trafia aktualizacja OTA |
| `littlefs` | 384 KB | system plików z `/state.json`, `/config.json`, `/presets.json` i `/paint.bin` |
| `coredump` | 64 KB | zrzut pamięci po awarii |

- Partycja plików ma typ `spiffs`, ale etykietę `littlefs`. `LittleFS.begin()` domyślnie szuka etykiety `spiffs`, więc etykietę trzeba podawać jawnie. Ostrzeżenie `gen_esp32part.py` o tej nazwie jest nieszkodliwe.
- `Storage` zapisuje plik najpierw jako `<nazwa>.tmp`, a potem zmienia jego nazwę, więc przerwany zapis nie psuje poprzedniej wersji.
- Każde pole pliku jest sprawdzane osobno: błędne albo brakujące pole dostaje wartość domyślną, a plik, którego nie da się odczytać, daje pełne wartości domyślne. Nowe pole w pliku musi mieć sensowną wartość domyślną dla plików ze starszych wersji.
- Efekty i parametry są w plikach zapisane po `id`, a nie po indeksie, więc dodanie, usunięcie albo zmiana kolejności efektów nie psuje plików.

## 5. Środowisko i kompilacja

### PlatformIO

- **Platforma:** pioarduino 55.03.311 (Arduino-ESP32 3.3.11, ESP-IDF 5.5). Nowsze wydanie 55.03.312 wymaga rdzenia PlatformIO `>=6.2.0`, a rdzeń pioarduino `6.2.0.post1` jest przez PlatformIO traktowany jako wersja wstępna starsza od 6.2.0, więc platforma odmawia instalacji. Przejście na nowszą wersję będzie możliwe, gdy to się zmieni.
- **Biblioteki** mają przypięte dokładne wersje: FastLED 3.10.8, AsyncTCP 3.5.0 i ESPAsyncWebServer 3.12.1 (obie z ESP32Async) oraz ArduinoJson 7.4.3. LittleFS pochodzi z frameworka. Aktualizacja biblioteki to świadoma zmiana, po której trzeba sprawdzić firmware na sprzęcie.
- **Windows:** `pio` jest w `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`. Pierwsza kompilacja po wyczyszczeniu `.pio` trwa kilkanaście minut, a `pio test -e native` kilka minut, głównie przez FastLED.
- **Pakiety współdzielone:** pioarduino przy instalacji usuwa z `~/.platformio/packages` inne wersje pakietów `framework-arduinoespressif32*`. Inne projekty na tym samym komputerze mogą wtedy pobierać swoje pakiety od nowa.

### Flagi

| Flaga | Gdzie | Znaczenie |
|---|---|---|
| `LED_PIN` | `[common] led_pin` | pin danych dla nowego urządzenia, zanim konfiguracja poda inny |
| `MAX_LEDS` | `[common] max_leds` | największa liczba diod; od niej zależą rozmiary płócien i slotów efektów |
| `LOG_LEVEL` | środowisko | 4 (debug) w `esp32dev`, 3 (info) w `esp32dev-release` |
| `LEDFRAME_CONSOLE` | `esp32dev` | konsola szeregowa |
| `LEDFRAME_LED_CHECK` | `esp32dev-ledcheck` | diagnostyka: licznik impulsów (PCNT) na pinie danych liczy zbocza narastające, które dotarły do diod. Poprawna klatka *n* diod ma ich dokładnie 24·*n*. Co 10 s log `LED check: … frames, … bad`. Służy do pomiaru błędnych klatek, np. przy zmianach wyjścia na diody albo przydziału rdzeni; nie trafia do wydań |
| `LEDFRAME_VARIANT` | `esp32dev`, `esp32dev-release` | wariant firmware w `GET /api/info`. Interfejs WWW wskazuje na jego podstawie plik `esp-led-frame-<wersja>-<wariant>-ota.bin` nowszego wydania. `esp32dev-ota` dziedziczy `esp32dev` |
| `FASTLED_RMT_MEM_BLOCKS_NETWORK_MODE=2` | `[esp32_base]` | większy bufor sterownika RMT przy działającym Wi-Fi ([IDEAS.md](IDEAS.md#wyjście-na-diody-odporne-na-wi-fi)) |
| `-Wall -Wextra` | `build_src_flags` | ostrzeżenia tylko dla kodu w `src/`; ostrzeżenia z FastLED i frameworka nie są nowymi ostrzeżeniami projektu |

## 6. Testy

```sh
pio test -e native                    # wszystkie zestawy
pio test -e native -f test_engine     # jeden zestaw
```

Testy wymagają gcc w `PATH`. Na Windows to np. MinGW-w64 w `C:\mingw64\bin`: w PowerShell `$env:Path = "C:\mingw64\bin;" + $env:Path` przed `pio test`. Terminal otwarty przed instalacją gcc trzeba uruchomić ponownie.

**Co jest kompilowane:** `env:native` kompiluje z `src/` tylko `core/`, `render/`, `effects/`, `state/` oraz `app/ConsoleParser.cpp`, `net/ApiJson.cpp`, `net/WsProtocol.cpp` i `net/DnsMessage.cpp`, bez `render/LedOutput.cpp` i `state/Storage.cpp`. Logika, którą da się sprawdzić bez sprzętu, ma więc być w tych plikach jako czyste funkcje, a moduły ESP32 tylko ją wołają.

**FastLED na komputerze** działa w wariancie stub. Każda flaga `env:native` jest potrzebna:

| Flaga | Powód |
|---|---|
| `lib_compat_mode = off` | FastLED deklaruje framework `arduino`, a bez tej opcji PlatformIO pomija bibliotekę w środowisku bez frameworka |
| `-include stdint.h` | kod w FastLED używa `uint64_t` bez dołączenia `<stdint.h>` |
| `-DFASTLED_STUB_IMPL` | na Windows FastLED nie wykrywa sam platformy stub |
| `-DARDUINO=10819 -DFASTLED_USE_STUB_ARDUINO` | makieta RMT w FastLED bez `ARDUINO` woła nieistniejącą funkcję |
| `-DARDUINOJSON_ENABLE_ARDUINO_*=0`, `-DARDUINOJSON_ENABLE_PROGMEM=0` | przy zdefiniowanym `ARDUINO` ArduinoJson szuka `Arduino.h` |
| `-lws2_32` (tylko Windows, `scripts/native_env.py`) | wariant stub FastLED używa gniazd sieciowych |

Skutki:

- W `env:native` makro `ARDUINO` jest zdefiniowane. Platformę rozpoznawaj przez `ESP32` albo `LEDFRAME_NATIVE`, nigdy przez `ARDUINO`.
- Kod kompilowany natywnie ma standard C++17.
- Niektóre metody `CRGB` mają ciało tylko w plikach `.hpp` FastLED i nie linkują się w testach (szczegóły w [ADDING_EFFECT.md](ADDING_EFFECT.md#8-testy)).

**Zestawy testów:**

| Zestawy | Co sprawdzają |
|---|---|
| `test_core`, `test_canvas`, `test_layout`, `test_palettes`, `test_output` | wersja i limity, płótno, odwzorowanie układu, palety i gamma, kolejność kolorów i wyjście |
| `test_rollback` | kiedy firmware na próbie po aktualizacji zostaje na stałe (`app/RollbackGuard.h`) |
| `test_registry`, `test_params`, `test_engine` | rejestr efektów, parametry i ich przycinanie, silnik (przejścia, harmonogram klatek, statystyki) |
| `test_effects` i `test_<efekt>` | efekty: `test_effects` dla `solid`, `rainbow`, `galaxy`, `color-flow`, `paint` i `status`, a pozostałe mają osobne zestawy |
| `test_state_json`, `test_file_format`, `test_presets`, `test_schedule` | stan, konfiguracja i presety w JSON, zgodność formatu plików ze starszymi wersjami, harmonogram |
| `test_api`, `test_ws`, `test_dns`, `test_console` | JSON API, protokół WebSocket, DNS captive portalu, konsola szeregowa |
| `test_render_dump` | nie jest testem: renderuje klatki dla `scripts/show_effect.py` i bez zmiennej `LEDFRAME_DUMP` nic nie robi |

Moduły tylko dla ESP32 (Wi-Fi, serwer, OTA, `LedOutput`, `Storage`) nie mają testów automatycznych. Zmianę w nich trzeba sprawdzić na płytce.

## 7. CI

`.github/workflows/ci.yml` przy każdym pushu i pull requeście:

- kompiluje `esp32dev` i `esp32dev-release` i zapisuje jako artefakty `firmware.bin`, `firmware.factory.bin`, `bootloader.bin` i `partitions.bin`. `firmware.factory.bin` to pełny obraz flash do wgrania od `0x0`, np. webowym flasherem (README, „Z przeglądarki, bez PlatformIO”), a `firmware.bin` służy do aktualizacji OTA;
- uruchamia `pio test -e native` na Linuksie;
- dla tagu `v*` (np. `v2.2.0`), gdy obie kompilacje i testy przejdą, tworzy wydanie na stronie Releases. Sprawdza, czy tag zgadza się z `kVersion` w `src/core/Version.h`, i dołącza pliki `esp-led-frame-<wersja>-<środowisko>-factory.bin` i `-ota.bin` dla obu środowisk oraz `SHA256SUMS.txt`. Do plików `-ota.bin` linkuje sprawdzanie nowej wersji w interfejsie WWW (`web/ui/settings.js`), więc zmiana ich nazw wymaga zmiany także tam.

CI używa PlatformIO 6.2.0 z pip i trzyma pakiety w cache zależnym od treści `platformio.ini`.

**Opis wydania** pisze `scripts/release_notes.sh`. Wszystko liczy od poprzedniego tagu `v*` w historii:

- **Zmiany:** każdy wiersz tabeli wersji z rozdziału 9 nowszy od poprzedniego tagu, aż do wydawanej wersji, od najnowszego. Trafiają tu też wersje, które nie dostały własnego tagu. Brak wiersza wydawanej wersji daje ostrzeżenie w przebiegu CI.
- **Commity od poprzedniego tagu:** tytuły commitów bez commitów scalających, z linkami, oraz link do porównania obu tagów na GitHubie.
- **Pliki:** tabela plików wydania.

Pierwsze wydanie, bez wcześniejszego tagu, dostaje wszystkie wiersze i commity. Opis trafia też do podsumowania przebiegu CI. Podgląd przed utworzeniem tagu: `bash scripts/release_notes.sh v2.3.1 HEAD` (w Git Bash na Windows). Skrypt czyta tabelę wersji z podanego commita, nie z drzewa roboczego.

## 8. Zasady kodu

- **Język:** kod, komentarze, logi i teksty UI po angielsku. Dokumentacja w `docs/` i README po polsku.
- **Nazwy:** klasy w `PascalCase`, metody i zmienne w `camelCase`, stałe `kPascalCase`, pola klasy z `_` na końcu, pliki jak klasy. Kod projektu jest w przestrzeni nazw `ledframe`.
- **Formatowanie:** `.clang-format` (styl Google, 100 kolumn).
- **Bez nowych ostrzeżeń** z `-Wall -Wextra` w `src/`.
- **Bez alokacji** w drodze renderowania, bez `String` do budowania odpowiedzi i bez `delay()`.
- **Logi** tylko makrami `LOG_E`, `LOG_W`, `LOG_I` i `LOG_D` z `core/Log.h`. Argumenty makr nie mogą mieć efektów ubocznych, bo przy niższym `LOG_LEVEL` wywołanie jest usuwane razem z nimi. Hasła nigdy nie trafiają do logów ani do odpowiedzi API.
- **Wątki:** moduły, które nie są oznaczone jako bezpieczne dla wątków, są używane z jednego zadania. Komentarz w nagłówku mówi z którego.
- **Zmiana API** wymaga zmiany [API.md](API.md), a nowe pole konfiguracji albo stanu także wartości domyślnej dla starszych plików i testu w `test_file_format`.

## 9. Wersje

Wersja firmware jest w `src/core/Version.h` i w teście `test_core`. Numeracja według semver: nowe funkcje i efekty podnoszą drugą liczbę, poprawki trzecią.

| Wersja | Najważniejsze zmiany |
|---|---|
| 2.0.0 | pierwsza wersja 2: nowa architektura, interfejs WWW, presety, captive portal, OTA, efekty `solid`, `rainbow`, `galaxy`, `color-flow`, `paint`, `fire`, `radial-rainbow` |
| 2.0.1 | mniej pamięci RAM, strumieniowy zapis plików, efekty w stałych slotach, dokładne FPS, ustawienia ditheringu i korekcji gamma (domyślnie wyłączonej) |
| 2.1.0 | korekcja gamma domyślnie włączona, nieliniowy suwak jasności, podgląd efektów w terminalu, efekty `plasma`, `noise`, `twinkle`, `breathe`, `rain`, `starfield`, `ripples` |
| 2.2.0 | efekty `metaballs`, `swirl`, `candle`, czas z NTP, harmonogram, karty w UI i pasek informacji, wybór pinu danych w UI |
| 2.2.1 | poprawka DNS: serwery DNS IPv6 z ogłoszeń routera (RDNSS), nieosiągalne bez adresu IPv6, są zastępowane serwerami IPv4 z DHCP, więc znów działa czas z NTP i harmonogram. Log połączenia Wi-Fi pokazuje bramę i serwery DNS |
| 2.3.0 | sprawdzanie nowej wersji na GitHubie w interfejsie WWW (Device → Firmware) z linkiem do pliku `-ota.bin` dla wariantu płytki, pola `variant` i `repo` w `GET /api/info` |
| 2.3.1 | poprawka bezpieczeństwa: żądania HTTP, uzgodnienie WebSocketu i aktualizacja firmware ze strony innej niż urządzenie (nagłówek `Origin`) dostają `403`, więc obca strona otwarta w przeglądarce w tej samej sieci nie zmieni stanu, konfiguracji ani firmware |
| 2.3.2 | wycofanie nieudanej aktualizacji: nowy firmware po OTA zostaje na stałe po 30 s normalnej pracy, a reset wcześniej (awaria, ciągłe restarty) przywraca poprzedni. Firmware, którego zadanie renderowania stoi, wycofuje się samo po 2 min. Restart na żądanie i kolejna aktualizacja zatwierdzają go od razu |
| 2.3.3 | poprawka bezpieczeństwa: ochrona przed DNS rebinding. Urządzenie odpowiada tylko pod swoimi nazwami (adres IP, `localhost`, nazwa urządzenia z domeną lokalną, np. `ledframe.local`, `ledframe.lan`), a pod każdą inną nazwą w nagłówku `Host` daje `403`, więc strona, której domena wskazuje urządzenie, nie odczyta ani nie zmieni jego ustawień |
| 2.3.4 | poprawka migotania: zapis plików (stan, presety, konfiguracja, obraz `paint`) i stanu firmware czeka na przerwę między klatkami, więc nie psuje klatki wysyłanej do diod. W pomiarze 93 uszkodzone klatki na 115 zapisów przed poprawką, 0 po niej. Diagnostyczne środowisko `esp32dev-ledcheck` liczy uszkodzone klatki |

### Gałęzie i wydania

- **`main`** zawiera tylko scalone zmiany, które przeszły CI i zostały sprawdzone na płytce.
- **Każda zmiana ma własną gałąź** od aktualnego `main`: `fix/<opis>` dla poprawek, `feature/<opis>` dla nowych funkcji i efektów.
- **Gałąź sama ustala swoją wersję:** podnosi `kVersion` w `src/core/Version.h` i oczekiwaną wartość w `test_core`, dopisuje wiersz do tabeli powyżej i poprawia numer wersji w README oraz na początku [API.md](API.md). Wiersz tabeli staje się częścią opisu wydania na GitHubie, a tytuły commitów trafiają do listy zmian od poprzedniego tagu ([rozdział 7](#7-ci)), więc powinny mówić, co się zmienia.
- **Najpierw poprawki, potem funkcje:** gdy czekają obie, najpierw scala się poprawkę, a gałąź funkcji zaczyna się od `main` z tą poprawką (albo jest na nią przenoszona przez `git rebase main`), żeby numery wersji szły po kolei.
- **Scalanie przez pull request** do `main` po zielonym CI (obie kompilacje i testy natywne), zwykłym merge (`git merge --no-ff`), więc każda zmiana zostaje w historii jako osobna gałąź.
- **Wydanie:** po scaleniu tag `v<wersja>` na `main` (np. `git tag v2.3.0 && git push origin v2.3.0`) uruchamia zadanie `release` w CI.
