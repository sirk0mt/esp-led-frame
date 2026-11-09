# esp-led-frame

Sterownik ramki z obrazem podświetlonym matrycą adresowalnych diod LED (WS2812B) na ESP32. Sterowanie z przeglądarki w telefonie albo komputerze, bez aplikacji i bez internetu.

Wersja firmware: **2.3.2** (`src/core/Version.h`). Wersja 2 jest napisana od nowa i nie czyta ustawień wersji 1.

## Spis treści

1. [Funkcje](#funkcje)
2. [Efekty](#efekty)
3. [Sprzęt](#sprzęt)
4. [Pierwsza instalacja (USB)](#pierwsza-instalacja-usb)
5. [Pierwsza konfiguracja Wi-Fi](#pierwsza-konfiguracja-wi-fi)
6. [Interfejs WWW](#interfejs-www)
7. [Konsola szeregowa](#konsola-szeregowa)
8. [Aktualizacja firmware](#aktualizacja-firmware)
9. [Co urządzenie zapamiętuje](#co-urządzenie-zapamiętuje)
10. [Rozwiązywanie problemów](#rozwiązywanie-problemów)
11. [Dla programistów](#dla-programistów)
12. [Dokumentacja](#dokumentacja)

## Funkcje

- **17 efektów** z ustawieniami (lista niżej). Każdy efekt pamięta swoje ustawienia, a przejścia między efektami są płynne.
- **Interfejs WWW wbudowany w firmware:**
  - włącznik i jasność w górnym pasku;
  - karty **Effects** (kafelki efektów, ustawienia efektu, podgląd na żywo), **Presets** i **Device** (ustawienia urządzenia);
  - pasek informacji na dole każdej strony;
  - 8 palet z podglądem;
  - tryb ciemny, układ dla telefonu, a na szerokim ekranie ustawienia efektu obok kafelków.

  Zmianę z jednego urządzenia od razu widać na pozostałych.
- **Presety:** efekt z ustawieniami, paleta i jasność zapisane pod nazwą i przywracane jednym kliknięciem.
- **Automatyczny zapis stanu:** kilka sekund po zmianie i przed każdym restartem. Po włączeniu prądu ramka wraca do ostatniego efektu.
- **Konfiguracja w UI:**
  - rozmiar i układ matrycy (narożnik startowy, obrót, wężyk, odbicia);
  - pin danych paska;
  - kolejność kolorów z wzorem testowym;
  - limit prądu, liczba klatek na sekundę, dithering i korekcja kolorów.
- **Czas i harmonogram:** czas z NTP w wybranej strefie czasowej oraz włączanie i wyłączanie o ustalonych godzinach w wybrane dni.
- **Wi-Fi:**
  - połączenie z siecią domową i adres `http://ledframe.local`;
  - bez zapisanej sieci (albo gdy sieć jest niedostępna) własna sieć `LedFrame-XXXX` ze stroną konfiguracji (captive portal) i czerwonym trójkątem na matrycy.
- **Aktualizacja przez sieć (OTA)** z UI z paskiem postępu, przez `curl` albo z PlatformIO, z opcjonalnym hasłem.
- **API REST i WebSocket** do własnych skryptów i integracji: [docs/API.md](docs/API.md).
- **Diagnostyka:** wersja, czas pracy, pamięć, FPS, czasy renderowania i przyczyna ostatniego restartu, z zapisem awarii (coredump).

## Efekty

Kolejność jak w UI. Efekty z paletą mają ustawienie **Palette** z własną, zapamiętaną paletą.

| `id` | Nazwa w UI | Co pokazuje | Ustawienia |
|---|---|---|---|
| `solid` | Solid | jeden kolor na całej matrycy | Color |
| `candle` | Candle | ciepłe, spokojne migotanie jak płomień świecy | Intensity, Speed, Color |
| `breathe` | Breathe | jeden kolor, który powoli jaśnieje i ciemnieje | Colors (kolor albo paleta), Color, Period, Min level, Palette |
| `rainbow` | Rainbow | przesuwający się gradient z palety pod wybranym kątem | Speed, Angle, Density, Palette |
| `plasma` | Plasma | nakładające się fale sinusoidalne (klasyczna plazma) | Speed, Scale, Palette |
| `metaballs` | Metaballs | miękkie plamy, które łączą się i rozdzielają | Speed, Count, Palette |
| `radial-rainbow` | Radial rainbow | pierścienie z palety od środka albo do środka | Speed, Density, Direction, Palette |
| `swirl` | Swirl | spirala z palety obracająca się wokół środka | Speed, Twist, Direction, Palette |
| `noise` | Noise | plamy unoszące się jak wosk w lampie lava | Speed, Scale, Palette |
| `ripples` | Ripples | krople wpadające do wody i rozchodzące się kręgi | Density, Damping, Palette |
| `galaxy` | Galaxy | losowe rozbłyski, które powoli gasną | Density, Fade, Colors (losowe albo z palety), Palette |
| `twinkle` | Twinkle | gwiazdy, które łagodnie zapalają się i gasną | Density, Speed, Palette, Background |
| `starfield` | Starfield | gwiazdy wylatujące od środka (warp) | Speed, Density, Colors (kolor albo losowe), Color |
| `color-flow` | Color flow | cała matryca w jednym, płynnie dryfującym kolorze | Speed, Max step |
| `rain` | Rain | krople spadające kolumnami ze smugą (cyfrowy deszcz) | Speed, Density, Colors (kolor albo losowe), Color |
| `fire` | Fire | ogień wznoszący się od dolnego wiersza | Speed, Cooling, Sparking, Palette |
| `paint` | Paint | obraz rysowany palcem albo myszą | narzędzia rysowania w UI |

Palety: `rainbow`, `party`, `ocean`, `forest`, `lava`, `heat`, `cloud` i `custom` (gradient czerwień → zieleń → błękit). Zakresy i wartości domyślne ustawień podaje `GET /api/effects` ([docs/API.md](docs/API.md#get-apieffects)), a ich znaczenie opisuje komentarz na początku pliku efektu w `src/effects/builtin/`.

Obraz `paint` jest zapisywany osobno i zostaje po zmianie efektu i po restarcie.

## Sprzęt

| Element | Wartość domyślna | Gdzie zmienić |
|---|---|---|
| Płytka | ESP32 z 4 MB flash (`esp32dev`) | `platformio.ini` |
| Diody | WS2812B, dane na GPIO 2 | UI: Device → LEDs → Data pin (działa po restarcie). Pin dla nowego urządzenia to `led_pin` w `[common]` w `platformio.ini` |
| Matryca | 15 × 10, start w lewym górnym rogu, bez wężyka | UI: Device → LEDs |
| Najwięcej diod | 1024 | `max_leds` w `[common]` w `platformio.ini` |
| Limit prądu | 5 V, 2000 mA | UI: Device → LEDs |

Pin danych można ustawić na GPIO 2, 4, 5, 13–19, 21–23, 25–27, 32 albo 33. Pozostałe piny są zajęte przez start płytki, konsolę albo pamięć flash, albo działają tylko jako wejścia.

**Podłączenie:**

- Diody zasilaj z osobnego zasilacza 5 V, a nie z pinu 5 V płytki. Masa zasilacza, paska i ESP32 musi być wspólna.
- Zasilacz dobierz do liczby diod. Przy pełnej bieli WS2812B pobiera do około 60 mA na diodę, czyli matryca 15 × 10 do około 9 A. Limit prądu w UI przyciemnia obraz tak, żeby nie przekroczyć ustawionej wartości. Ustaw go na wartość, którą zasilacz naprawdę daje.
- ESP32 wysyła sygnał 3,3 V, a WS2812B zasilane 5 V zwykle go przyjmuje. Jeśli pojedyncze diody mają zły kolor, pomaga konwerter poziomów 3,3 → 5 V na linii danych. Na wielu płytkach GPIO 2 ma też diodę LED płytki, co może zakłócać sygnał. Wtedy wybierz inny pin.

## Pierwsza instalacja (USB)

Pierwsze wgranie wersji 2 musi być przez USB, bo zmienia tablicę partycji (`partitions.csv`). Kolejne aktualizacje mogą iść przez sieć.

Są dwie drogi: z przeglądarki, bez instalowania czegokolwiek, albo z PlatformIO, które jest potrzebne do zmian w kodzie.

### Z przeglądarki, bez PlatformIO

Potrzebny jest komputer z Chrome, Edge albo Operą (Firefox i Safari nie obsługują portu szeregowego ze strony WWW) i kabel USB do płytki.

1. **Pobierz firmware.** Na stronie [Releases](https://github.com/sirk0mt/esp-led-frame/releases) pobierz z najnowszego wydania plik `esp-led-frame-<wersja>-esp32dev-factory.bin`. Niewydaną wersję z dowolnej gałęzi można pobrać z Actions → ostatni udany przebieg **CI** → Artifacts → `firmware-esp32dev` (plik `firmware.factory.bin` w archiwum). Artefakty wymagają zalogowania na GitHubie, a GitHub usuwa je po pewnym czasie (domyślnie po 90 dniach).
2. **Otwórz webowy flasher Espressifa:** [espressif.github.io/esptool-js](https://espressif.github.io/esptool-js/).
3. **Połącz się z płytką:** podłącz ją przez USB, kliknij „Connect” i wybierz jej port z listy. Jeśli połączenie się nie udaje, przytrzymaj przycisk BOOT na płytce w czasie łączenia.
4. **Wgraj plik:** w polu adresu wpisz `0x0`, wybierz `firmware.factory.bin` i kliknij „Program”. Wgrywanie trwa około minuty.
5. **Uruchom ramkę:** naciśnij na płytce przycisk EN (RESET) albo odłącz i podłącz USB. Dalej postępuj jak w rozdziale [Pierwsza konfiguracja Wi-Fi](#pierwsza-konfiguracja-wi-fi).

`firmware.factory.bin` to pełny obraz pamięci od adresu `0x0`: bootloader, tablica partycji i firmware w jednym pliku, więc nie trzeba podawać kilku adresów. Nie zawiera systemu plików, więc wgranie go na płytkę z wersją 2 zostawia ustawienia, sieć Wi-Fi i presety. Żeby zacząć od zera, kliknij przed wgraniem „Erase Flash”.

Kolejne aktualizacje też nie wymagają narzędzi: plik `esp-led-frame-<wersja>-esp32dev-ota.bin` z wydania (w artefakcie CI to `firmware.bin`) wgrywa się z UI (Device → Firmware, rozdział [Aktualizacja firmware](#aktualizacja-firmware)). Do aktualizacji przez sieć nie używaj pliku `factory`: urządzenie go odrzuci.

Jeśli port płytki nie pojawia się na liście, komputer nie ma sterownika jej układu USB. Najczęściej to CP210x albo CH340, a nazwa układu jest zwykle nadrukowana obok złącza USB. Windows 10 i 11 zwykle instalują go same, a w przeciwnym razie trzeba go pobrać od producenta układu. Pomaga też inny kabel: część kabli USB służy tylko do ładowania.

Pliki `esp32dev-release` (w CI artefakt `firmware-esp32dev-release`) zawierają wersję bez konsoli szeregowej i bez logów debug. Do codziennego użytku obie działają tak samo.

### Z PlatformIO

1. Zainstaluj [PlatformIO](https://platformio.org/) (np. rozszerzenie do VS Code). Platforma i biblioteki pobiorą się same przy pierwszej kompilacji, która trwa kilkanaście minut.
2. Podłącz płytkę i wgraj firmware:

   ```sh
   pio run -e esp32dev -t upload
   pio device monitor        # log startu, 115200 bit/s
   ```

   W Windows PowerShell zamiast `pio` użyj `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"`.
3. System plików (LittleFS) formatuje się sam przy pierwszym starcie. Interfejs WWW jest wbudowany w firmware, więc nie trzeba wgrywać osobnego obrazu plików.

Środowiska w `platformio.ini`:

| Środowisko | Do czego |
|---|---|
| `esp32dev` | firmware z logami debug i konsolą szeregową, wgrywane przez USB (domyślne) |
| `esp32dev-ota` | to samo firmware, wgrywane przez sieć (ArduinoOTA) |
| `esp32dev-release` | firmware bez logów debug i bez konsoli szeregowej |
| `native` | testy na komputerze, bez płytki |

## Pierwsza konfiguracja Wi-Fi

Bez zapisanej sieci urządzenie tworzy własną, otwartą sieć `LedFrame-XXXX` (`XXXX` to końcówka adresu MAC), a matryca pokazuje czerwony trójkąt.

1. Połącz telefon albo komputer z `LedFrame-XXXX`. System zwykle sam otwiera stronę konfiguracji. Jeśli nie, otwórz `http://192.168.4.1/setup.html`.
2. Wybierz sieć z listy, wpisz hasło i nazwę urządzenia (domyślnie `ledframe`), a potem „Connect”.
3. Ramka łączy się z siecią i wyłącza swoją. Połącz telefon albo komputer z tą samą siecią i otwórz `http://ledframe.local`. Jeśli system nie obsługuje adresów `.local`, adres IP znajdziesz na liście urządzeń w routerze albo w logu startu (`connected to '...', IP ...`).

Druga droga to konsola szeregowa w środowisku `esp32dev` (rozdział [Konsola szeregowa](#konsola-szeregowa)):

```
wifi "Nazwa mojej sieci" haslo1234
```

**Zachowanie przy problemach z siecią:**

- Jeśli przy starcie połączenie nie uda się w ciągu 20 s, urządzenie włącza własną sieć `LedFrame-XXXX`.
- Po utracie połączenia urządzenie przez 2 minuty próbuje połączyć się ponownie w tle, a dopiero potem włącza własną sieć.
- W trybie własnej sieci co 5 minut ponawia próbę połączenia z zapisaną siecią. Gdy ktoś jest połączony z `LedFrame-XXXX` (np. ze stroną konfiguracji), czeka z próbą, najwyżej 30 minut.

## Interfejs WWW

Pod `http://ledframe.local` (albo `http://192.168.4.1` w trybie własnej sieci):

- **Górny pasek:** włącznik, jasność i stan połączenia. Suwak jasności jest nieliniowy, żeby niskie jasności dało się ustawić dokładnie.
- **Effects:**
  - **Live preview:** podgląd tego, co pokazuje matryca, włączany przełącznikiem;
  - **Effects:** kafelek wybiera efekt;
  - **Effect settings:** ustawienia wybranego efektu. Dla `paint` są tu narzędzia rysowania: kolor, pędzel, gumka, wypełnienie i czyszczenie.
- **Presets:**
  - kliknięcie stosuje preset;
  - pole nazwy i „Save” zapisują bieżący stan;
  - „✎” zmienia nazwę presetu, a „✕” go usuwa.
- **Device**, w grupach:
  - **LEDs:** rozmiar i układ matrycy, pin danych, napięcie i limit prądu, FPS, dithering, korekcja kolorów, kolejność kolorów i wzór testowy;
  - **Network:** stan Wi-Fi, skan sieci, nazwa sieci i hasło;
  - **System:** nazwa urządzenia (adres `<nazwa>.local`), hasło OTA, strefa czasowa i restart;
  - **Schedule:** do 8 godzin włączania i wyłączania w wybrane dni tygodnia;
  - **Firmware:** wgranie nowego firmware.
- **Pasek informacji** na dole: wersja, godzina, Wi-Fi, FPS i czas pracy. Po kliknięciu pokazuje szczegóły: pamięć, liczbę diod, czasy renderowania i wysyłania klatki oraz przyczynę ostatniego restartu.

Zmiany działają od razu i zapisują się same. Do zapamiętania konkretnych ustawień służą presety.

Harmonogram działa tylko wtedy, gdy urządzenie zna czas, czyli po połączeniu z siecią z dostępem do internetu. Serwer czasu (domyślnie `pool.ntp.org`) zmienia się przez API (`ntpServer` w [docs/API.md](docs/API.md#post-apiconfig)).

## Konsola szeregowa

Konsola działa w środowisku `esp32dev` (115200 bit/s, `pio device monitor`). Nie ma jej w `esp32dev-release`.

| Komenda | Działanie |
|---|---|
| `effect <id>` | przełącza efekt, np. `effect fire` |
| `bri <0-255>` | ustawia jasność |
| `param <id> <wartość>` | ustawia parametr aktywnego efektu; wartość może być szesnastkowa, np. `param color 0xFF0000` |
| `order <0-5>` | kolejność kolorów: RGB, RBG, GRB, GBR, BRG, BGR |
| `test` | wzór testowy do następnej komendy innej niż `order` |
| `info` | wersja, stan i statystyki silnika |
| `preset save <nazwa>` | zapisuje bieżący efekt, paletę i jasność jako preset (istniejąca nazwa jest nadpisywana) |
| `preset apply <id>` | stosuje preset |
| `wifi <ssid> [hasło]` | łączy z siecią; bez hasła z siecią otwartą, a `wifi ""` zostawia tylko własną sieć urządzenia |

Słowa ze spacjami podaje się w cudzysłowie: `wifi "Moja sieć" "tajne hasło"`. Wewnątrz cudzysłowu `\"` oznacza cudzysłów, a `\\` backslash. Listę komend konsola wypisuje po starcie i po każdej błędnej komendzie, np. po wpisaniu `?`.

## Aktualizacja firmware

Zbuduj firmware: `pio run -e esp32dev`. Plik powstaje w `.pio/build/esp32dev/firmware.bin`. Stan, konfiguracja, presety i obraz `paint` zostają.

- **Nowa wersja z GitHuba:** Device → Firmware, „Check for a new version”. Gdy na stronie Releases jest nowsza wersja, strona podaje link do pliku `…-ota.bin` dla tej płytki. Pobierz go i wgraj formularzem poniżej. Urządzenie niczego nie pobiera ani nie instaluje samo.
- **Z przeglądarki:** Device → Firmware, wybierz `firmware.bin` i „Upload firmware”. Strona przeładuje się po restarcie urządzenia. Plik może pochodzić z własnej kompilacji albo z artefaktu CI ([Z przeglądarki, bez PlatformIO](#z-przeglądarki-bez-platformio)).
- **Z wiersza poleceń:** `curl -F firmware=@.pio/build/esp32dev/firmware.bin http://ledframe.local/api/ota`. W Windows PowerShell użyj `curl.exe`. Z hasłem OTA dodaj `-u ota:HASLO`.
- **Z PlatformIO:** `pio run -e esp32dev-ota -t upload`. Z hasłem ustaw najpierw zmienną środowiskową `PLATFORMIO_UPLOAD_FLAGS=--auth=HASLO`.

Nieudana aktualizacja nie psuje działającego firmware: nowy obraz trafia do drugiej partycji i jest sprawdzany przed przełączeniem. Nowy firmware, który w ciągu pierwszych 30 s ulega awarii albo restartuje się w kółko, oraz firmware z zawieszonym renderowaniem jest wycofywany, a urządzenie wraca do poprzedniej wersji. Dlatego przez 30 s po aktualizacji nie odłączaj zasilania. Szczegóły: [docs/API.md](docs/API.md#wycofanie-nieudanej-aktualizacji).

## Co urządzenie zapamiętuje

Wszystko jest w systemie plików LittleFS na płytce:

| Plik | Zawartość | Kiedy jest zapisywany |
|---|---|---|
| `/state.json` | włącznik, jasność, aktywny efekt, czas przejścia, ustawienia wszystkich efektów | 5 s po ostatniej zmianie |
| `/config.json` | matryca, pin, limit prądu, FPS, Wi-Fi, nazwa urządzenia, hasło OTA, strefa czasowa, harmonogram | od razu po zmianie |
| `/presets.json` | presety (do 32) | od razu po zmianie |
| `/paint.bin` | obraz efektu `paint` | 5 s po ostatniej zmianie |

Przed restartem i przed aktualizacją OTA oczekujące pliki są zapisywane od razu. Uszkodzony albo niepełny plik nie blokuje startu: brakujące i błędne wartości dostają wartości domyślne.

**Powrót do ustawień fabrycznych:** wyczyść całą pamięć flash i wgraj firmware ponownie przez USB:

```sh
pio run -e esp32dev -t erase
pio run -e esp32dev -t upload
```

Kasuje to też zapisaną sieć Wi-Fi i hasło OTA. Żeby tylko zapomnieć sieć Wi-Fi, ustaw pustą nazwę sieci w Device → Network albo wpisz w konsoli `wifi ""`.

## Rozwiązywanie problemów

| Objaw | Co sprawdzić |
|---|---|
| Matryca pokazuje czerwony trójkąt | Urządzenie nie ma sieci albo nie może się z nią połączyć i działa we własnej sieci `LedFrame-XXXX`. Skonfiguruj Wi-Fi jak w rozdziale [Pierwsza konfiguracja Wi-Fi](#pierwsza-konfiguracja-wi-fi). |
| Telefon po połączeniu z `LedFrame-XXXX` nie otwiera żadnej strony, nawet `http://192.168.4.1/setup.html` | Wyłącz na chwilę VPN i ochronę sieci publicznych (np. „Bezpieczne Wi-Fi” w Samsungu) albo użyj komputera. Przyczyna tego przypadku nie jest jeszcze wyjaśniona. |
| `http://ledframe.local` nie działa | System albo przeglądarka nie obsługuje adresów `.local` (część telefonów z Androidem). Użyj adresu IP z routera albo z logu startu. Nowa nazwa urządzenia działa dopiero po restarcie. |
| Kolory są zamienione, np. czerwony świeci na zielono | Device → LEDs: „Show test pattern” i zmieniaj „Color order”, aż pierwsza dioda będzie biała, a kolejne trzy części: czerwona, zielona i niebieska. |
| Obraz jest odwrócony, przesunięty albo co drugi wiersz idzie w złą stronę | Device → LEDs: narożnik startowy, obrót, wężyk (Serpentine) i odbicia. Wzór testowy pokazuje, gdzie zaczyna się pasek. |
| Pojedyncze diody na chwilę mają zły kolor, zwłaszcza przy otwartym podglądzie na żywo | Zakłócenia sygnału przy ruchu Wi-Fi albo za słaby sygnał 3,3 V. Pomaga konwerter poziomów, inny pin niż GPIO 2 i wyłączenie podglądu. Opis przyczyny: [docs/IDEAS.md](docs/IDEAS.md). |
| Obraz jest ciemniejszy, niż powinien | Limit prądu przyciemnia jasne obrazy. Sprawdź wartość w Device → LEDs (0 wyłącza limit). |
| Harmonogram nie działa | Urządzenie nie zna czasu: pasek informacji pokazuje „time not synchronized”. Czas jest dostępny tylko po połączeniu z siecią z dostępem do internetu. |
| Czas się nie synchronizuje, choć urządzenie jest w sieci | Serwer NTP wymaga działającego DNS. Log startu (konsola szeregowa) pokazuje w wierszu `gateway …, DNS …` serwery DNS otrzymane z routera. Jeśli pierwszy z nich nie odpowiada, popraw ustawienia DHCP w routerze. Serwery DNS IPv6, które router ogłasza osobno, urządzenie samo zastępuje serwerami IPv4, bo bez adresu IPv6 nie może z nich korzystać. |
| Strona pokazuje „Too many open pages” | Urządzenie obsługuje naraz 4 otwarte strony. Zamknij nieużywane karty. |
| Urządzenie się zrestartowało | Pasek informacji → „Last restart”. Po awarii jest tam też zadanie i adresy z coredumpu ([docs/API.md](docs/API.md#diagnostyka-awarii)). |

## Dla programistów

- **Struktura:** `src/` to firmware w przestrzeni nazw `ledframe`, `web/` to interfejs (moduły ES, bez kroku budowania), `test/` to testy natywne (Unity), a `scripts/` to skrypty kompilacji i podglądu efektów.
- **Testy:** `pio test -e native`. Wymagają gcc w `PATH`, na Windows np. MinGW-w64. CI (GitHub Actions) kompiluje firmware i uruchamia testy przy każdym pushu.
- **Nowy efekt** to jeden plik `.cpp`, który sam się rejestruje i pojawia się w API i UI bez dalszych zmian: [docs/ADDING_EFFECT.md](docs/ADDING_EFFECT.md).
- **Podgląd efektu bez sprzętu:** `python scripts/show_effect.py fire` pokazuje klatki w terminalu.
- **Architektura, środowisko i zasady kodu:** [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Dokumentacja

| Dokument | Zawartość |
|---|---|
| [docs/API.md](docs/API.md) | API REST, WebSocket, captive portal, OTA i diagnostyka awarii |
| [docs/ADDING_EFFECT.md](docs/ADDING_EFFECT.md) | jak napisać, przetestować i sprawdzić nowy efekt |
| [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) | architektura firmware i UI, środowisko, testy, CI i zasady kodu |
| [docs/IDEAS.md](docs/IDEAS.md) | pomysły na kolejne efekty i zmiany techniczne |
