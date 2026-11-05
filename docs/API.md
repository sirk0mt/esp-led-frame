# API urządzenia esp-led-frame v2

Interfejs REST na porcie 80 i WebSocket `/ws`. Wszystkie odpowiedzi API są w JSON (`Content-Type: application/json`), poza `GET /api/paint`. Przykłady używają adresu `ledframe.local`. W trybie AP adres to `192.168.4.1`. Opis dotyczy firmware 2.3.0. Przy polach dodanych później podana jest wersja, od której działają.

API nie ma uwierzytelniania, poza opcjonalnym hasłem do aktualizacji firmware. Każdy w tej samej sieci może zmieniać stan i konfigurację.

## Spis treści

- [Wiersz poleceń](#wiersz-poleceń)
- [Zasady wspólne](#zasady-wspólne)
- [Stan](#stan): `GET`, `POST /api/state`
- [Efekty i palety](#efekty-i-palety): `GET /api/effects`, `GET /api/palettes`, `GET /api/paint`
- [Konfiguracja](#konfiguracja): `GET`, `POST /api/config`
- [Presety](#presety): `/api/presets`
- [Wi-Fi](#wi-fi): `/api/wifi/scan`
- [System](#system): `GET /api/info`, `POST /api/test-pattern`, `POST /api/restart`
- [Interfejs WWW](#interfejs-www)
- [Captive portal](#captive-portal)
- [WebSocket `/ws`](#websocket-ws)
- [Aktualizacja firmware](#aktualizacja-firmware): `POST /api/ota`, ArduinoOTA
- [Diagnostyka awarii](#diagnostyka-awarii)

| Metoda i ścieżka | Działanie |
|---|---|
| `GET /api/state` | bieżący stan |
| `POST /api/state` | częściowa zmiana stanu |
| `GET /api/effects` | efekty ze schematem parametrów |
| `GET /api/palettes` | palety z próbkami kolorów |
| `GET /api/paint` | obraz efektu `paint` (binarnie) |
| `GET /api/config` | konfiguracja, bez haseł |
| `POST /api/config` | częściowa zmiana konfiguracji |
| `GET /api/presets` | lista presetów |
| `POST /api/presets` | zapis bieżącego stanu jako preset |
| `POST /api/presets/{id}` | zmiana nazwy presetu |
| `DELETE /api/presets/{id}` | usunięcie presetu |
| `POST /api/presets/{id}/apply` | zastosowanie presetu |
| `POST /api/wifi/scan` | start skanu Wi-Fi |
| `GET /api/wifi/scan` | wynik skanu |
| `GET /api/info` | informacje o systemie i diagnostyka |
| `POST /api/test-pattern` | wzór testowy |
| `POST /api/restart` | restart |
| `POST /api/ota` | aktualizacja firmware |
| `GET /ws` | WebSocket |

## Wiersz poleceń

Przykłady są w dwóch wariantach:

- **bash** (Linux, macOS, Git Bash na Windows) używa `curl`.
- **Windows PowerShell** używa `Invoke-RestMethod`. W Windows PowerShell 5.1 `curl` to alias `Invoke-WebRequest` z innymi parametrami, więc polecenia `curl` z tego dokumentu tam nie działają. Prawdziwy curl jest dostępny jako `curl.exe`, ale PowerShell 5.1 usuwa cudzysłowy z JSON w argumentach programów. Dla JSON używaj więc funkcji `Send-Led` poniżej, a `curl.exe` tylko do uploadu firmware.

bash:

```sh
DEV=http://ledframe.local
```

PowerShell (wklej raz w sesji):

```powershell
$DEV = "http://ledframe.local"
# Wysyła tablicę @{...} jako JSON w UTF-8: działają spacje, apostrofy i polskie znaki.
function Send-Led($Path, $Data) {
  $json = ConvertTo-Json $Data -Depth 5 -Compress
  Invoke-RestMethod -Method Post -Uri "$DEV$Path" -ContentType "application/json; charset=utf-8" `
    -Body ([Text.Encoding]::UTF8.GetBytes($json))
}
```

Przy kodzie błędu `Invoke-RestMethod` zgłasza wyjątek z treścią odpowiedzi, np. `{"error":"bri must be an integer 0-255"}`. Jest ona w `$_.ErrorDetails.Message`.

Odpowiedzi mają nagłówek `Content-Type: application/json; charset=utf-8`, więc polskie znaki wyświetlają się poprawnie także w Windows PowerShell 5.1.

## Zasady wspólne

- **Body żądania** to obiekt JSON wysłany z nagłówkiem `Content-Type: application/json`. Inna wartość daje `400`. Body bez tego nagłówka, np. z `curl -d`, też działa, ale serwer dekoduje je wtedy jak formularz: znak `&` rozdziela body, a `+` staje się spacją.
- **Nieznane pola** są ignorowane i logowane w konsoli szeregowej.
- **Znane pole ze złym typem albo wartością spoza zakresu** daje `400` z treścią `{"error":"..."}`.
- **Wartości parametrów efektu** to liczby całkowite. Silnik sam je przycina do zakresu ze schematu (`GET /api/effects`), więc wartość spoza zakresu nie jest błędem.
- **Zmiany są asynchroniczne.** `POST` odpowiada od razu, a zmiana wchodzi w życie w następnej klatce, po kilku do kilkunastu milisekund. Aktualny stan pokazuje `GET /api/state`.
- **Limity body:** `/api/state` 2048 B, `/api/config` 1024 B, `/api/presets` i `/api/test-pattern` 256 B. Większe body daje `413`.
- **Pełna kolejka komend** daje `503`. Wtedy wyślij żądanie ponownie.

| Kod | Znaczenie |
|---|---|
| 200 | OK |
| 201 | Utworzono preset |
| 202 | Skan Wi-Fi uruchomiony |
| 400 | Błędne dane, treść `{"error":"..."}` |
| 404 | Nieznany adres albo preset |
| 409 | Brak aktywnego efektu albo pełna lista presetów |
| 413 | Za duże body |
| 503 | Kolejka komend pełna albo brak pamięci |

## Stan

### `GET /api/state`

```sh
curl $DEV/api/state
```

```powershell
Invoke-RestMethod "$DEV/api/state"
```

```json
{"version":12,"on":true,"bri":128,"effect":"solid","transition":500,"palette":0,
 "params":{"color":16777215}}
```

- **`version`** rośnie przy każdej zmianie stanu.
- **`params`** to parametry aktywnego efektu, według `id` ze schematu.
- **`palette`** to indeks palety używanej przez aktywny efekt.

### `POST /api/state`

Częściowa zmiana. Wszystkie pola są opcjonalne.

| Pole | Typ | Zakres |
|---|---|---|
| `on` | bool | |
| `bri` | liczba | 0–255 |
| `transition` | liczba, ms | 0–10000 |
| `effect` | `id` efektu | z `GET /api/effects`, bez ukrytych |
| `params` | obiekt `id parametru → wartość` | wartości dla `effect`, a bez niego dla aktywnego efektu |
| `palette` | indeks albo `id` palety | z `GET /api/palettes` |

W `params` parametr typu `toggle` przyjmuje też `true` i `false`, a typ `color` przyjmuje też tekst `"#RRGGBB"`. Nieznane parametry są ignorowane. Kolejność zastosowania: zasilanie, jasność, czas przejścia, parametry, zmiana efektu z przejściem, paleta.

```sh
curl -X POST -H 'Content-Type: application/json' $DEV/api/state -d '{"bri":200}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/state -d '{"on":false}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/state -d '{"effect":"solid","params":{"color":"#FF4000"}}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/state -d '{"params":{"color":16711680},"transition":1000}'
```

```powershell
Send-Led /api/state @{ bri = 200 }
Send-Led /api/state @{ on = $false }
Send-Led /api/state @{ effect = "solid"; params = @{ color = "#FF4000" } }
Send-Led /api/state @{ params = @{ color = 16711680 }; transition = 1000 }
```

Odpowiedź: `{"ok":true}`.

## Efekty i palety

### `GET /api/effects`

Widoczne efekty w kolejności UI, ze schematem parametrów. UI buduje z tego panel parametrów, więc nowy efekt nie wymaga zmian w API ani w UI.

```sh
curl $DEV/api/effects
```

```powershell
Invoke-RestMethod "$DEV/api/effects" | ConvertTo-Json -Depth 6
```

```json
[{"id":"solid","name":"Solid","order":10,"is2d":false,"usesPalette":false,"static":true,
  "widget":null,
  "params":[{"id":"color","label":"Color","kind":"color","default":16777215,"min":0,"max":16777215}]}]
```

| `kind` | Pola | Kontrolka w UI |
|---|---|---|
| `slider` | `min`, `max`, `default` | suwak |
| `toggle` | `min` 0, `max` 1, `default` | przełącznik |
| `color` | `min` 0, `max` 0xFFFFFF, `default` jako 0xRRGGBB | wybór koloru |
| `select` | `options` (lista nazw), `default` jako indeks opcji | lista |
| `palette` | `min` 0, `max` (ostatni indeks palety), `default` | lista palet z podglądem |

`widget` to nazwa dodatkowego komponentu UI (np. `"paint"`) albo `null`.

### `GET /api/palettes`

```sh
curl $DEV/api/palettes
```

```powershell
Invoke-RestMethod "$DEV/api/palettes"
```

```json
[{"index":0,"id":"rainbow","name":"Rainbow","custom":false,"colors":["#FF0000","#D52A00","..."]}]
```

Każda paleta ma 16 próbek do podglądu. `custom` oznacza paletę użytkownika: gradient przez trzy kolory, zapisane w pliku stanu. Domyślnie to czerwień, zieleń i błękit. API i UI nie pozwalają jeszcze ich zmienić.

| Indeks | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |
|---|---|---|---|---|---|---|---|---|
| `id` | `rainbow` | `party` | `ocean` | `forest` | `lava` | `heat` | `cloud` | `custom` |

### `GET /api/paint`

Efekt `paint` pokazuje obraz rysowany w UI. Obraz istnieje niezależnie od tego, który efekt jest aktywny: zostaje po zmianie efektu i po restarcie.

- **Rozmiar:** zawsze taki jak matryca (`layout.width × layout.height`). Po zmianie układu zostaje lewa górna część, a nowe piksele są czarne.
- **Zapis:** do `/paint.bin` 5 s po ostatniej zmianie, a przed restartem i OTA od razu.
- **Zmiany:** obraz zmienia się tylko przez binarne wiadomości WebSocket (niżej).

`GET /api/paint` zwraca cały obraz jako `application/octet-stream`, w tym samym formacie co plik `/paint.bin`. Liczby są little-endian:

| Bajty | Zawartość |
|---|---|
| 0–1 | `LP` |
| 2 | wersja formatu, `1` |
| 3 | `0` |
| 4–5 | szerokość |
| 6–7 | wysokość |
| od 8 | piksele wierszami (`y · szerokość + x`), po 3 bajty `r`, `g`, `b` |

Dla matrycy 15×10 to 8 + 450 = 458 B.

```sh
curl -s $DEV/api/paint -o paint.bin
```

```powershell
Invoke-WebRequest "$DEV/api/paint" -OutFile paint.bin
```

## Konfiguracja

### `GET /api/config`

Hasła nie wracają w odpowiedzi. Pole `hasPassword` mówi tylko, czy hasło jest ustawione.

```sh
curl $DEV/api/config
```

```powershell
Invoke-RestMethod "$DEV/api/config" | ConvertTo-Json -Depth 4
```

```json
{"layout":{"width":15,"height":10,"serpentine":false,"origin":"top-left","rotation":0,
           "flipX":false,"flipY":false},
 "colorOrder":0,"ledPin":2,"power":{"volts":5,"milliamps":2000},"fps":50,"dither":true,"gamma":true,
 "hostname":"ledframe","wifi":{"ssid":"","hasPassword":false},"ota":{"hasPassword":false},
 "timezone":"CET-1CEST,M3.5.0,M10.5.0/3","ntpServer":"pool.ntp.org",
 "schedule":[{"enabled":true,"days":[0,1,2,3,4],"time":"07:30","action":"on"}]}
```

### `POST /api/config`

Częściowa zmiana. Pola są takie same jak w `GET`, a hasła podaje się jako `wifi.password` i `ota.password`.

| Pole | Zakres |
|---|---|
| `layout.width`, `layout.height` | od 1, razem najwyżej `MAX_LEDS` (1024) |
| `layout.origin` | `top-left`, `top-right`, `bottom-left`, `bottom-right` |
| `layout.rotation` | 0, 90, 180, 270 |
| `layout.serpentine`, `layout.flipX`, `layout.flipY` | bool |
| `colorOrder` | 0–5: RGB, RBG, GRB, GBR, BRG, BGR |
| `ledPin` | GPIO linii danych paska: 2, 4, 5, 13–19, 21–23, 25–27, 32 albo 33. Domyślnie `led_pin` z `platformio.ini` (2). Działa po restarcie; do tego czasu `GET /api/info` podaje w `ledPin` pin, który działa teraz. Pominięte są piny startowe 0 i 12, konsola (1, 3), flash (6–11) i piny tylko wejściowe (34–39). Plik konfiguracji bez tego pola albo z innym pinem daje domyślny |
| `power.volts` | 1–24 |
| `power.milliamps` | 0–20000, gdzie 0 wyłącza limit |
| `fps` | 10–120 |
| `dither` | bool, domyślnie `true`. Dithering FastLED: wygładza ciemne kolory w kolejnych klatkach. Działa od następnej klatki, a bieżąca klatka jest wysyłana od razu ponownie, więc zmianę widać też na efekcie statycznym. Plik konfiguracji z 2.0.1 i starszych nie ma tego pola i daje `true` |
| `gamma` | bool, domyślnie `true` (od 2.1.0; w 2.0.1 `false`). Plik konfiguracji bez tego pola daje `true`. Korekcja gamma 2,2 kolorów wybranych na ekranie: parametrów `Color` (np. `solid`), obrazu `paint` i palety `custom`. Wbudowane palety się nie zmieniają. Wartości w stanie, presetach, `/api/paint` i `/api/palettes` zostają takie, jak je wybrano (sRGB), a korekcja dotyczy tylko tego, co dostają diody i podgląd na żywo. Działa od następnej klatki, także dla efektu statycznego |
| `hostname` | 1–32 znaki: małe litery, cyfry i `-`, nie na początku ani na końcu |
| `wifi.ssid` | do 32 znaków, pusty oznacza tylko tryb AP |
| `wifi.password` | puste albo 8–64 znaki |
| `ota.password` | do 64 znaków |
| `timezone` | reguła strefy czasowej POSIX TZ (jak zmienna `TZ`), domyślnie `CET-1CEST,M3.5.0,M10.5.0/3` (Polska i Europa Środkowa z czasem letnim). 1–63 znaki, zaczyna się literą albo `<` (nazwa w nawiasach, np. `<+0545>-5:45`), dalej tylko litery, cyfry i `+ - , . / : < >`. Sprawdzana jest tylko składnia znaków, a nie sens reguły. Działa w ciągu sekundy, bez restartu. Od 2.2.0 |
| `schedule` | harmonogram włączania i wyłączania (od 2.2.0): lista do 8 wpisów `{"enabled", "days", "time", "action"}`. `days` to niepusta lista dni od 0 (poniedziałek) do 6 (niedziela), `time` to czas lokalny `GG:MM` (00:00–23:59) w strefie `timezone`, `action` to `on` albo `off`, a `enabled` (bool, domyślnie `true`) pozwala wyłączyć wpis bez usuwania. Pole zastępuje całą listę, a `[]` ją czyści. Każdy błąd daje 400 i zostawia starą listę. Opis działania jest pod tabelą |
| `ntpServer` | serwer czasu SNTP, nazwa albo adres IPv4, 1–64 znaki: litery, cyfry, `.` i `-`. Domyślnie `pool.ntp.org`. Synchronizacja startuje po połączeniu z siecią (tryb STA), a zmiana serwera w ciągu sekundy uruchamia ją ponownie. W trybie AP czasu nie ma. W UI tego pola nie ma. Od 2.2.0 |

- **Brakujące pola** zostają bez zmian.
- **Hasła:** brak pola hasła zostawia obecne hasło, a `""` je usuwa.
- **Układ:** pola układu łączą się z obecnym układem, a całość musi być poprawna.
- **Harmonogram (`schedule`):** wpis działa raz, w minucie, w której wypada jego godzina, w wybrane dni, tak jak przełącznik w UI (zmieniony stan `on` trafia do pliku stanu i do wszystkich stron). Przy dwóch wpisach na tę samą minutę wygrywa ostatni na liście. Bez znanego czasu (`/api/info`: `time.synced` = `false`) nic się nie dzieje, a minuta, która minęła, gdy urządzenie było wyłączone albo nie znało czasu, nie jest nadrabiana. Przy zmianie na czas letni (zegar przeskakuje z 2:00 na 3:00) wpisy z pominiętej godziny nie działają tego dnia.
- **Działanie zmian:** zmiana układu, FPS, kolejności kolorów i limitu prądu działa od razu. Nowa sieć Wi-Fi jest próbowana od razu, a w trybie AP przy działającym AP. Nowy `hostname` i `ledPin` działają po restarcie.

```sh
curl -X POST -H 'Content-Type: application/json' $DEV/api/config -d '{"layout":{"width":15,"height":10,"serpentine":true}}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/config -d '{"colorOrder":2}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/config -d '{"timezone":"GMT0BST,M3.5.0/1,M10.5.0"}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/config -d '{"schedule":[{"days":[0,1,2,3,4],"time":"07:30","action":"on"},{"days":[0,1,2,3,4,5,6],"time":"23:00","action":"off"}]}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/config -d '{"wifi":{"ssid":"Dom","password":"tajnehaslo"}}'
# SSID z apostrofem w bash: zamknij cudzysłów, dodaj \' i otwórz go ponownie
curl -X POST -H 'Content-Type: application/json' $DEV/api/config -d '{"wifi":{"ssid":"There'\''s no place like 127.0.0.1","password":"tajnehaslo"}}'
```

```powershell
Send-Led /api/config @{ layout = @{ width = 15; height = 10; serpentine = $true } }
Send-Led /api/config @{ colorOrder = 2 }
Send-Led /api/config @{ wifi = @{ ssid = "There's no place like 127.0.0.1"; password = "tajnehaslo" } }
```

Odpowiedź: `{"ok":true}`.

W konsoli szeregowej SSID albo hasło ze spacjami podaje się w cudzysłowie: `wifi "There's no place like 127.0.0.1" tajnehaslo`. Wewnątrz cudzysłowu `\"` oznacza cudzysłów, a `\\` backslash.

## Presety

Preset to efekt z parametrami, paleta i jasność. Ma stały numer `id` od 1 do 255. Lista mieści 32 presety.

```sh
curl $DEV/api/presets
```

```json
[{"id":1,"name":"Wieczór","effect":"solid","bri":60,"palette":0,"params":{"color":16744448}}]
```

| Żądanie | Działanie | Odpowiedź |
|---|---|---|
| `POST /api/presets` z `{"name":"..."}` | zapisuje bieżący efekt, parametry, paletę i jasność. Istniejąca nazwa jest nadpisywana | `201 {"id":n}`, `400` dla złej nazwy, `409` przy pełnej liście |
| `POST /api/presets/{id}` z `{"name":"..."}` | zmienia nazwę presetu, a zawartość i `id` zostają | `200`, `400` dla złej nazwy, `404`, `409` gdy inny preset ma tę nazwę |
| `DELETE /api/presets/{id}` | usuwa preset | `200`, `404` |
| `POST /api/presets/{id}/apply` | stosuje preset z przejściem, bez zmiany zasilania | `200`, `404` |

Zmiany presetów nie są rozgłaszane przez WebSocket. Interfejs wczytuje listę ponownie po własnych zmianach, po ponownym połączeniu i po powrocie karty na pierwszy plan.

```sh
curl -X POST -H 'Content-Type: application/json' $DEV/api/presets -d '{"name":"Wieczór"}'
curl -X POST -H 'Content-Type: application/json' $DEV/api/presets/1 -d '{"name":"Wieczór przy kominku"}'
curl -X POST $DEV/api/presets/1/apply
curl -X DELETE $DEV/api/presets/1
```

```powershell
Invoke-RestMethod "$DEV/api/presets"
Send-Led /api/presets @{ name = "Wieczór" }
Send-Led /api/presets/1 @{ name = "Wieczór przy kominku" }
Invoke-RestMethod -Method Post "$DEV/api/presets/1/apply"
Invoke-RestMethod -Method Delete "$DEV/api/presets/1"
```

## Wi-Fi

| Żądanie | Działanie |
|---|---|
| `POST /api/wifi/scan` | uruchamia skan w tle, odpowiedź `202 {"scanning":true}` |
| `GET /api/wifi/scan` | wynik ostatniego skanu, do 20 sieci od najsilniejszej |

```sh
curl -X POST $DEV/api/wifi/scan
sleep 4
curl $DEV/api/wifi/scan
```

```powershell
Invoke-RestMethod -Method Post "$DEV/api/wifi/scan"
Start-Sleep -Seconds 4
(Invoke-RestMethod "$DEV/api/wifi/scan").networks
```

```json
{"scanning":false,"networks":[{"ssid":"Dom","rssi":-48,"secure":true}]}
```

## System

### `GET /api/info`

```sh
curl $DEV/api/info
```

```powershell
Invoke-RestMethod "$DEV/api/info" | ConvertTo-Json -Depth 4
```

```json
{"version":"2.3.0","variant":"esp32dev","repo":"sirk0mt/esp-led-frame","buildDate":"Nov  3 2026 20:54:25","uptime":360,"freeHeap":146364,
 "minFreeHeap":101992,"leds":150,"maxLeds":1024,"testPattern":false,"ledPin":2,
 "ledPins":[2,4,5,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33],
 "engine":{"targetFps":50,"fps":50,"shownFps":0,"renderUsAvg":2,"renderUsMax":30,"showUsAvg":0},
 "wifi":{"mode":"connected","ssid":"Dom","ip":"192.168.1.40","rssi":-52},
 "time":{"synced":true,"sntp":true,"local":"2026-11-03 21:05:07","weekday":1},
 "resetReason":"power-on","lastCrash":null}
```

- **`version`** i **`buildDate`** to wersja firmware i chwila kompilacji.
- **`variant`** (od 2.3.0) to wariant firmware, czyli środowisko PlatformIO, z którego go zbudowano (`esp32dev` albo `esp32dev-release`), a **`repo`** to repozytorium GitHub z wydaniami. Interfejs WWW używa ich do sprawdzenia nowej wersji ([Aktualizacja firmware](#aktualizacja-firmware)).
- **`uptime`** to czas od startu w sekundach.
- **`freeHeap`** to wolna pamięć RAM w bajtach, a **`minFreeHeap`** najmniejsza wolna pamięć od startu.
- **`leds`** to liczba diod w bieżącym układzie (`layout.width × layout.height`).
- **`engine`:** `targetFps` to ustawione FPS, a czasy są w mikrosekundach. `showUsAvg` to średni czas wysłania klatki do diod.
- **`engine.fps`** to liczba klatek, które silnik policzył w ciągu sekundy, także tych bez zmian obrazu. Przy działającym silniku równa się `targetFps` (od 2.0.1 dokładnie, np. 60, a nie 62). Przy wzorze testowym i w czasie aktualizacji wynosi 0. Do 2.0.0 było to liczba przebiegów pętli renderowania. `shownFps` to liczba klatek wysłanych do diod, a efekt statyczny ma tu 0. `renderUsAvg` i `renderUsMax` dotyczą policzonych klatek.
- **`maxLeds`** to limit `layout.width × layout.height` (stała kompilacji `MAX_LEDS`).
- **`testPattern`** mówi, czy diody pokazują teraz wzór testowy.
- **`ledPin`** to GPIO, na którym od startu działa pasek, a **`ledPins`** to lista pinów, które można ustawić w `ledPin` konfiguracji.
- **`time`** (od 2.2.0): `synced` mówi, czy zegar urządzenia zna czas. Zna go po synchronizacji z serwerem NTP, a także po programowym restarcie (np. po OTA), bo zegar ESP32 go przetrzymuje. `sntp` mówi, czy synchronizacja z serwerem działa: `false` w trybie AP i przed połączeniem z siecią. Nazwę serwera NTP rozwiązują serwery DNS z DHCP. Gdy router ogłasza też serwery DNS IPv6 (RDNSS), urządzenie przywraca serwery IPv4, bo bez adresu IPv6 nie może z nich korzystać. Bez znanego czasu pól `local` i `weekday` nie ma (`{"synced":false,"sntp":true}`). Potem `local` to czas lokalny w strefie `timezone` z konfiguracji (`RRRR-MM-DD GG:MM:SS`), a `weekday` to dzień tygodnia od 0 (poniedziałek) do 6 (niedziela).
- **`wifi.mode`** to `connecting`, `connected` albo `ap`. `rssi` (siła sygnału w dBm) jest tylko w trybie `connected`. W trybie AP pojawiają się też `apName`, `apClients` (liczba połączonych urządzeń) i `retrying`, czyli czy urządzenie właśnie próbuje połączyć się z zapisaną siecią.
- **`resetReason`** i **`lastCrash`** opisuje rozdział [Diagnostyka awarii](#diagnostyka-awarii).

### `POST /api/test-pattern`

`{"on":true}` pokazuje wzór testowy do sprawdzenia kolejności kolorów i początku paska. Pierwsza dioda paska jest biała, a reszta podzielona na trzy części: czerwoną, zieloną i niebieską, w kolejności fizycznej, bez odwzorowania układu. `{"on":false}` wraca do efektu.

- **Zmiana kolejności kolorów** (`POST /api/config` z `colorOrder`) zostawia wzór na diodach, więc od razu widać jej skutek.
- **Każda inna zmiana stanu** też kończy wzór.
- **Stan wzoru** podaje pole `testPattern` w `GET /api/info`.

Odpowiedź: `{"ok":true}`, a dla złego body `400`.

```sh
curl -X POST -H 'Content-Type: application/json' $DEV/api/test-pattern -d '{"on":true}'
```

```powershell
Send-Led /api/test-pattern @{ on = $true }
Send-Led /api/test-pattern @{ on = $false }
```

### `POST /api/restart`

Odpowiada `{"ok":true}`, zapisuje oczekujące pliki i restartuje urządzenie po 0,5 s.

```sh
curl -X POST $DEV/api/restart
```

```powershell
Invoke-RestMethod -Method Post "$DEV/api/restart"
```

## Interfejs WWW

`GET /` zwraca `index.html`. Każdy plik z katalogu `web/` jest dostępny pod swoją ścieżką, np. `web/ui/presets.js` pod `/ui/presets.js`, a strona konfiguracji Wi-Fi `web/setup.html` pod `/setup.html`. Nieznana ścieżka spoza `/api/` daje `404`, a w trybie AP przekierowanie opisane w [Captive portal](#captive-portal).

- **Wbudowanie w firmware:** pliki są skompresowane gzipem przez `scripts/embed_web.py` przy kompilacji. Nie trzeba wgrywać systemu plików. Z plików JS, CSS i HTML skrypt najpierw usuwa wcięcia i komentarze w osobnych liniach, więc urządzenie wysyła je w takiej postaci, a nie tak jak w repozytorium.
- **Nagłówki odpowiedzi:**
  - `Content-Encoding: gzip`;
  - `ETag` liczony z treści pliku;
  - `Cache-Control: no-cache`, więc przeglądarka zawsze sprawdza, czy ma aktualną wersję.
- **Odpowiedź `304`:** jeśli żądanie ma `If-None-Match` z tym samym ETag, serwer odpowiada `304 Not Modified` bez treści. Po wgraniu firmware z nowym interfejsem ETag się zmienia i przeglądarka od razu pobiera nową wersję.

Serwer obsługuje tylko `GET`, nie `HEAD`. Dlatego przykłady pobierają stronę, wypisują nagłówki (`-D -`) i odrzucają treść, zamiast używać `curl -I`:

```bash
curl -s -D - -o /dev/null http://ledframe.local/                                # 200, nagłówek ETag
curl -s -D - -o /dev/null -H 'If-None-Match: "<etag>"' http://ledframe.local/   # 304
```

```powershell
curl.exe -s -D - -o NUL "$DEV/"                                       # 200, nagłówek ETag
curl.exe -s -D - -o NUL -H 'If-None-Match: \"<etag>\"' "$DEV/"        # 304
```

## Captive portal

W trybie AP (sieć `LedFrame-XXXX`, bez hasła) urządzenie działa jak portal konfiguracyjny.

- **DNS:** serwer DNS odpowiada adresem 192.168.4.1 na każdą nazwę.
- **Przekierowanie:** każde żądanie HTTP do innej nazwy hosta niż `192.168.4.1` albo `<hostname>.local`, na dowolną ścieżkę, dostaje `302` na `http://192.168.4.1/setup.html`.
- **Testy łączności:** dotyczy to także testów, którymi system sprawdza dostęp do internetu:
  - Android: `connectivitycheck.gstatic.com/generate_204`;
  - iOS i macOS: `captive.apple.com/hotspot-detect.html`;
  - Windows: `www.msftconnecttest.com/connecttest.txt` i `www.msftncsi.com/ncsi.txt`;
  - Firefox: `detectportal.firefox.com`.

  Zamiast oczekiwanej odpowiedzi system dostaje przekierowanie i sam otwiera stronę konfiguracji.
- **Pełne UI:** pod `http://192.168.4.1/` działa także w trybie AP. Poza trybem AP nic nie jest przekierowywane.

`setup.html` zawiera listę sieci ze skanu, hasło i nazwę urządzenia. Zapis wysyła `POST /api/config` z `wifi` i `hostname`.

- **Puste hasło** przy tej samej nazwie sieci zostawia zapisane hasło, a przy nowej sieci oznacza sieć otwartą.
- **Zmiana nazwy urządzenia** kończy się restartem, bo nowa nazwa działa po restarcie. Bez zmiany nazwy urządzenie od razu próbuje połączyć się z siecią przez najwyżej 15 s.
- **Wynik:**
  - Po udanym połączeniu urządzenie wyłącza swoją sieć, więc telefon traci połączenie i strona to zgłasza.
  - Jeśli połączenie się nie uda, urządzenie zostaje w trybie AP z `wifi.retrying: false` w `GET /api/info`, a strona prosi o sprawdzenie hasła.

Drugą drogą konfiguracji jest komenda `wifi` w konsoli szeregowej przez USB (README, rozdział „Konsola szeregowa”).

## WebSocket `/ws`

Kanał do synchronizacji stanu między klientami na żywo. Zmiana z telefonu od razu pojawia się na komputerze.

```js
const ws = new WebSocket(`ws://${location.host}/ws`);
ws.onmessage = (event) => {
  if (typeof event.data === "string") {
    const msg = JSON.parse(event.data);
    if (msg.error) console.warn(msg.error); else render(msg);  // msg = stan
  }
};
ws.onopen = () => ws.send(JSON.stringify({ bri: 120 }));
```

### Od urządzenia do klienta

- **Zaraz po połączeniu** klient dostaje bieżący stan, w tym samym formacie co `GET /api/state` (z polem `version`).
- **Po każdej zmianie stanu** wszyscy klienci dostają nowy stan, z dowolnego źródła: WebSocket, REST, konsola, preset. Rozgłoszenia są wysyłane najwyżej co 50 ms. Przy serii zmian pośrednie wersje mogą zostać pominięte, ale ostatnia zawsze dociera.
- **Błąd wiadomości od klienta** wraca tylko do nadawcy jako `{"error":"..."}`.

### Od klienta do urządzenia: tekst

Obiekt JSON z częściową zmianą stanu, z tymi samymi polami i zasadami co `POST /api/state`: `on`, `bri`, `transition`, `effect`, `params`, `palette`. Poprawna wiadomość nie dostaje osobnej odpowiedzi. Potwierdzeniem jest rozgłoszony nowy stan.

```json
{"params":{"color":"#00FF80"}}
```

**Sprawdzanie połączenia:** tekst `ping` (dokładnie te 4 znaki, nie JSON) dostaje odpowiedź `pong`, tylko do nadawcy. Przeglądarka nie może wysyłać ramek ping protokołu WebSocket, a restart albo odłączenie urządzenia nie zamyka połączenia po stronie klienta. Interfejs WWW wysyła więc `ping` co 10 s. Jeśli przez 5 s nie przyjdzie żadna wiadomość, zrywa połączenie i łączy się ponownie.

| Ograniczenie | Wartość |
|---|---|
| Liczba klientów | 4. Piąty jest zamykany z kodem 1013 i powodem `too many clients` |
| Długość wiadomości | 2048 B |
| Fragmentacja | niedozwolona: wiadomość musi przyjść w jednej ramce, inaczej `{"error":"fragmented messages are not supported"}` |
| Długie ramki | ramka dłuższa niż segment TCP (około 1,4 KB) jest składana z części. Naraz tylko jedna taka wiadomość, a inny klient w tym czasie (najwyżej 2 s) dostaje `{"error":"busy, send again"}` |

### Wiadomości binarne

Pierwszy bajt określa typ.

| Pierwszy bajt | Kierunek | Przeznaczenie |
|---|---|---|
| `0x01` | klient → urządzenie | zmiany obrazu efektu `paint` |
| `0x02` | oba | podgląd matrycy na żywo: subskrypcja i klatki |

Inny pierwszy bajt albo pusta wiadomość daje `{"error":"unknown binary message"}`.

#### Zmiany obrazu `paint` (`0x01`)

Drugi bajt to komenda. Liczby są little-endian.

| Komenda | Dalsze bajty | Działanie |
|---|---|---|
| `0x00` piksele | dla każdego piksela 7 bajtów: `x` (u16), `y` (u16), `r`, `g`, `b` | ustawia piksele |
| `0x01` clear | brak | czyści obraz na czarno |
| `0x02` fill | `r`, `g`, `b` | wypełnia cały obraz kolorem |

- **Ramka:** wiadomość musi przyjść w jednej ramce, najwyżej 292 piksele (2 + 292 · 7 = 2046 B). Więcej pikseli wyślij w kilku wiadomościach.
- **Piksele poza obrazem** (`x ≥ szerokość` albo `y ≥ wysokość`) są pomijane. Pozostałe piksele z tej samej wiadomości są zastosowane, a nadawca dostaje `{"error":"N paint pixels outside the picture"}`.
- **Zła długość albo nieznana komenda** odrzuca całą wiadomość z błędem, np. `{"error":"paint pixels take 7 bytes each"}`.
- **Pełna kolejka:** `{"error":"paint queue full, send again"}` oznacza, że urządzenie nie przyjęło żadnego piksela z tej wiadomości.
- **Potwierdzenia:** poprawna wiadomość nie dostaje odpowiedzi i nie rozgłasza stanu. Obraz zmienia się także wtedy, gdy aktywny jest inny efekt, a widać go po przełączeniu na `paint`.
- **Synchronizacja:** wiadomość, z której urządzenie przyjęło co najmniej jeden piksel albo komendę, trafia bez zmian do wszystkich pozostałych klientów WebSocket. Nadawca jej nie dostaje. Dzięki temu wszystkie otwarte edytory pokazują ten sam obraz. Po ponownym połączeniu edytor pobiera obraz od nowa z `GET /api/paint`.

Przykład: piksel (2, 1) na czerwono to bajty `01 00 02 00 01 00 FF 00 00`.

#### Podgląd na żywo (`0x02`)

| Wiadomość | Kierunek | Znaczenie |
|---|---|---|
| `02 01` | klient → urządzenie | włącz podgląd dla tego klienta |
| `02 00` | klient → urządzenie | wyłącz podgląd |
| `02 10`, szerokość (u16), wysokość (u16), piksele | urządzenie → klient | klatka: płótno logiczne wierszami, po 3 bajty `r`, `g`, `b` |

- **Zawartość klatki:** to, co pokazuje matryca, w układzie logicznym (`y · szerokość + x`, przed odwzorowaniem na fizyczne diody), bez jasności i limitu prądu. Przy wyłączonym zasilaniu klatka jest czarna.
- **Kiedy przychodzą klatki:**
  - tylko gdy obraz się zmienił, najwyżej około 15 na sekundę;
  - efekt statyczny wysyła klatkę tylko po zmianie;
  - nowy subskrybent dostaje od razu bieżącą klatkę.
- **Wolny klient:** dostaje najwyżej jedną klatkę naraz. Jeśli poprzednia nie została jeszcze wysłana, pośrednie klatki są pomijane.
- **Koniec subskrypcji:** po rozłączeniu urządzenie ją zapomina, więc klient włącza ją ponownie po połączeniu. Bez subskrybentów zadanie renderowania nie kopiuje płótna.
- **Zła wiadomość** `0x02`, np. `02 10` od klienta, daje `{"error":"preview takes 0x02 0x01 (start) or 0x02 0x00 (stop)"}`.

Dla matrycy 15×10 klatka ma 6 + 450 = 456 B, czyli około 6,8 KB/s.

```sh
# Test z wiersza poleceń (np. websocat, dostępny też na Windows):
websocat ws://ledframe.local/ws
{"bri":30}
```

## Aktualizacja firmware

Urządzenie samo nie sprawdza ani nie pobiera nowych wersji. W interfejsie WWW (Device → Firmware) przycisk „Check for a new version” pyta API GitHuba z przeglądarki (`GET https://api.github.com/repos/<repo>/releases/latest`) i porównuje tag najnowszego wydania z `version` z `GET /api/info`. Gdy wydanie jest nowsze, strona podaje link do pliku `esp-led-frame-<wersja>-<variant>-ota.bin` i do opisu wydania. Pobrany plik wgrywa się formularzem poniżej, czyli przez `POST /api/ota`. Przeglądarka potrzebuje do tego dostępu do internetu, a GitHub przyjmuje bez logowania 60 zapytań na godzinę z jednego adresu IP.

### `POST /api/ota`

Upload `firmware.bin` jako pole formularza multipart. Plik powstaje w `.pio/build/esp32dev/firmware.bin` po `pio run -e esp32dev`.

```sh
curl -F firmware=@.pio/build/esp32dev/firmware.bin $DEV/api/ota
# z ustawionym hasłem OTA (użytkownik zawsze "ota"):
curl -u ota:HASLO -F firmware=@.pio/build/esp32dev/firmware.bin $DEV/api/ota
```

W PowerShell użyj `curl.exe`, czyli prawdziwego curl z Windows 10 i nowszych, a nie aliasu `curl`. Upload nie ma JSON w argumencie, więc problem z cudzysłowami tu nie występuje:

```powershell
curl.exe -F "firmware=@.pio\build\esp32dev\firmware.bin" "$DEV/api/ota"
curl.exe -u ota:HASLO -F "firmware=@.pio\build\esp32dev\firmware.bin" "$DEV/api/ota"
```

Przebieg:
1. Urządzenie sprawdza hasło. Wymaga go tylko wtedy, gdy hasło OTA jest ustawione w konfiguracji.
2. Wygasza diody, wstrzymuje efekty i zapisuje oczekujące pliki stanu.
3. Zapisuje obraz do drugiej partycji aplikacji i sprawdza go.
4. Odpowiada `{"ok":true,"bytes":n,"restarting":true}`, a po 1 s się restartuje. Stan, konfiguracja i presety zostają zachowane.

| Kod | Znaczenie |
|---|---|
| 200 | obraz zapisany, restart za 1 s |
| 400 | brak pliku, obraz niepełny albo nie jest firmware ESP32. Diody wracają do pracy |
| 401 | złe albo brakujące hasło OTA, z nagłówkiem `WWW-Authenticate: Basic`. Żądanie z nagłówkiem `X-Requested-With` (tak wysyła interfejs WWW) dostaje 401 bez `WWW-Authenticate`, bo inaczej przeglądarka pokazuje własne okno logowania i wstrzymuje żądanie |
| 409 | trwa inna aktualizacja |
| 500 | błąd zapisu do flash |

Zerwanie połączenia w trakcie przerywa aktualizację, a urządzenie wraca do pracy na dotychczasowym firmware.

### ArduinoOTA (espota)

Urządzenie słucha też ArduinoOTA na porcie UDP 3232, z tym samym hasłem. Środowisko PlatformIO `esp32dev-ota` buduje ten sam firmware co `esp32dev` i wgrywa go przez sieć.

```sh
pio run -e esp32dev-ota -t upload                              # na ledframe.local
pio run -e esp32dev-ota -t upload --upload-port 192.168.0.14   # na adres IP
PLATFORMIO_UPLOAD_FLAGS=--auth=HASLO pio run -e esp32dev-ota -t upload   # z hasłem
```

```powershell
pio run -e esp32dev-ota -t upload
$env:PLATFORMIO_UPLOAD_FLAGS = "--auth=HASLO"; pio run -e esp32dev-ota -t upload   # z hasłem
Remove-Item Env:PLATFORMIO_UPLOAD_FLAGS
```

Nowe hasło OTA działa dla ArduinoOTA po restarcie, a dla `/api/ota` od razu.

## Diagnostyka awarii

`GET /api/info` podaje przyczynę ostatniego restartu w polu `resetReason`. Możliwe wartości to `power-on`, `software`, `panic`, `task-watchdog`, `interrupt-watchdog`, `watchdog`, `brownout`, `external` i `unknown`. Jeśli przed restartem firmware się zawiesił i zapisał coredump, pole `lastCrash` podaje zadanie, PC i backtrace:

```json
"resetReason":"panic",
"lastCrash":{"task":"render","pc":"0x400D1234","backtrace":["0x400D1234","0x40081ABC"],
             "backtraceCorrupted":false}
```

Bez zapisanego coredumpu `lastCrash` ma wartość `null`.

Adresy można zamienić na miejsca w kodzie narzędziem `xtensa-esp32-elf-addr2line -e .pio/build/esp32dev/firmware.elf 0x400D1234`. Plik `firmware.elf` musi pochodzić z tej samej kompilacji co firmware na urządzeniu. Narzędzie jest w pakiecie `toolchain-xtensa-esp-elf` w `~/.platformio/packages/`. Coredump jest kasowany po odczycie przy starcie, więc ta sama awaria pojawia się tylko do następnego restartu.

Przy podłączonym USB `pio device monitor` sam zamienia adresy z komunikatu o awarii na miejsca w kodzie (filtr `esp32_exception_decoder` w `platformio.ini`).
