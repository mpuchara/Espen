# Espen — polski asystent głosowy na ESP32-P4

Pierwszy port [Xiaozhi ESP32](https://github.com/78/xiaozhi-esp32) v2.5.2 dla GUITION JC4880P443C_I_W(_Y), z ekranem ST7701S 480×800, GT911, mikrofonem ES8311 i Wi-Fi przez ESP32-C6. Niezależny od Home-Buddy. Wersja 0.1.9 jest eksperymentalna; wymaga testu na sprzęcie.

## Instalacja
[Instalator Espen](https://mpuchara.github.io/Espen/) — Chrome/Edge na komputerze, kabel USB z danymi. Wybierz Install i port **P4**, nie C6. Jeśli potrzebne, przytrzymaj BOOT podczas resetu i ponów połączenie. Pierwsza instalacja zastępuje poprzedni firmware i może skasować konfigurację; wykonaj kopię. Instalator obsługuje tylko wariant P4 rev1 (16 MB flash) tej konkretnej płytki, nie inne P4.

Po starcie wybierz **Ustawienia → Wi-Fi** na ekranie, wpisz SSID i hasło, naciśnij **Zapisz i połącz**. Płytka uruchomi się ponownie. Możesz też wybrać **Konfiguruj przez telefon** i połączyć się z hotspotem Espen-XXXX (portal http://192.168.4.1). ESP32-C6 obsługuje wyłącznie 2,4 GHz; sieć o nazwie 5G zadziała tylko jeśli ma również pasmo 2,4 GHz. Dane Wi-Fi są przechowywane wyłącznie w NVS urządzenia, a nie w kodzie lub instalatorze. Można zapisać kilka sieci. Dłuższe przytrzymanie BOOT otwiera tryb konfiguracji Wi-Fi. Na ekranie pojawi się kod aktywacji. W [Xiaozhi Console](https://xiaozhi.me/console/agents) wybierz agenta Ala → Devices/Add Device i wpisz kod. Nie wpisuj haseł ani kodów do GitHuba. Nie trzeba zakładać kolejnego agenta.

Dotknij **Mów / Zakończ** lub naciśnij BOOT, aby rozpocząć wypowiedź, ponownie aby ją zakończyć. Po odpowiedzi można rozpocząć następną. Ekran pokazuje transkrypcję oraz odpowiedzi przesyłane przez serwer. W pierwszej wersji mikrofon nie jest aktywowany słowem budzącym. Wzmacniacz pozostaje wyłączony; obsługa audio upstream jest zachowana na przyszłość. Kamera i karta SD nie są inicjalizowane.

Xiaozhi to usługa chmurowa: wypowiedzi są wysyłane do serwera w celu rozpoznania mowy i wygenerowania odpowiedzi. Tekst i audio odpowiedzi pochodzą od usługi. Nie jest to rozwiązanie offline.

## Budowanie
ESP-IDF 6.1 (upstream v2.5.2). W środowisku IDF:
```sh
bash scripts/build.sh
```
Skrypt pobiera ustalony commit upstream, nakłada adapter Espen i uruchamia kanoniczny build Xiaozhi. Wynik: upstream/build/merged-binary.bin, a strona gotowa do publikacji w dist/. W repo zachowujemy jedynie własne zmiany, nie kopiujemy całego upstream.

GitHub Actions buduje firmware i publikuje stronę tylko po udanym buildzie. W Settings → Pages → Source wybierz GitHub Actions, jeśli Pages nie jest jeszcze włączone. Binarka jest również artefaktem Actions. Nie ma automatycznego OTA z serwera Xiaozhi (zachowujemy aktywację i provisioning).

## Testy na sprzęcie
1. Boot bez restartów, ekran i dotyk (pionowo), Wi-Fi C6.
2. Sparowanie kodem z Ala; sprawdzić polskie litery i przewijanie odpowiedzi.
3. Podczas wypowiedzi obserwować log EspenMic: peak i RMS powinny wzrastać podczas mowy. Nie są logowane próbki audio.
4. Sprawdzić 10 kolejnych rozmów, odłączenie Wi-Fi i reconnect. Jeśli C6 nie komunikuje się, zweryfikować jego firmware ESP-Hosted; instalator nie nadpisuje C6.
5. Na później: fizyczny głośnik na złączu płytki i aktywacja GPIO11, regulacja głośności, wake word i animacje twarzy.

## Źródła i licencje
Xiaozhi (MIT) — autorzy upstream, pin commit 49b4380b3faa0ceadd28a3e78aaf0f311c46c57e. Sekwencja ST7701S pochodzi z ultramcu/guition-jc4880p4-bsp (MIT) przez Home-Buddy, z zachowanym nagłówkiem. Pinout i panel sprawdzone w projekcie Home-Buddy; nowy port audio i interakcja wymagają walidacji. Patrz THIRD_PARTY_NOTICES.md.

## Diagnostyka czarnego ekranu (0.1.3)
Po włączeniu powinny pojawić się czerwony, zielony i niebieski pas przez około 700 ms, następnie UI. Test jest wykonywany przed inicjalizacją Wi-Fi i audio. Jeżeli nie pojawia się obraz lub urządzenie restartuje się, w instalatorze otwórz **Logi USB**, wybierz port P4 i naciśnij RESET (nie BOOT). Skopiuj log od startu. Szukamy linii EspenBoard, błędów PSRAM, bootloadera, panic i komunikatów LCD. W tej wersji używamy dwóch natywnych framebufferów DPI i pełnego renderowania LVGL bez generycznego sterownika MipiLcdDisplay. Działa w pionie. To port sprawdzonego podejścia Home-Buddy, nie zweryfikowany jeszcze na fizycznym urządzeniu w Espen.

## 0.1.4 – naprawa resetów podczas startu ESP-Hosted
Log z fizycznego GUITION rev1.3 pokazuje prawidłowy start flash DIO i 32 MB PSRAM @80 MHz, a następnie `assert failed: sdio_mempool_create sdio_drv.c:273 (buf_mp_g)` przed inicjalizacją wyświetlacza. W esp_hosted v2.13.1 ten assert oznacza niepowodzenie utworzenia wspólnej puli buforów transportu SDIO. Najbardziej prawdopodobny problem to alokacja ciągłego bufora DMA. Używamy oficjalnego CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM, czyli preferowania DMA-capable PSRAM z fallbackiem do wewnętrznego RAM. Kolejki RX/TX zmniejszone z 20 do 10; podwójny bufor RX wyłączony. Nie wyłączamy SDIO, Wi-Fi ani asercji. Dodana obsługa wykrytej kości BOYA. Build sprawdza rzeczywisty sdkconfig oraz archiwizuje ELF/map do dekodowania panic. Test fizyczny pozostaje wymagany.

## 0.1.5 — połączenie Wi-Fi i ustawienia Xiaozhi
Porównanie z Home-Buddy (20ae213): bezpośrednie esp_wifi_set_config + esp_wifi_connect zamiast skanowania aplikacyjnego przed każdą próbą, pamięć Wi-Fi na P4 (WIFI_STORAGE_RAM po stronie C6), SDIO 20 MHz, próby z przerwą 2–15 s i bez przypinania BSSID. Zachowujemy portal AP, zapamiętane profile i API zdarzeń Xiaozhi. Biblioteka esp-wifi-connect 3.3.3 jest przypięta i modyfikowana w lokalnym komponencie. Portal najpierw weryfikuje hasło, potem przełącza na STA; zapis z ekranu nadal restartuje urządzenie, aby zachować spójny stan protokołu. Ograniczenie: czasy połączenia trzeba zmierzyć na płytce. Ustawienia → Xiaozhi / parowanie pokazują kod, stan serwera, adres HTTPS i umożliwiają restart po zmianie adresu. Domyślny adres to https://api.tenclass.net/xiaozhi/ota/. Agent Ala jest przypisywany w konsoli chmurowej, a nie wybierany lokalnie. Kod nie jest przechowywany w NVS. Kamera pozostaje wyłączona w tym wydaniu (Home-Buddy używa innej wersji IDF i osobnego toru ISP/CSI; nie kopiujemy go bez walidacji).

## 0.1.6 — poprawka kompilacji
Usunięto pozostałe odwołanie do MAX_RETRY w logu ponawiania aktywacji. Zachowano bezpośrednie połączenie STA, limit odstępu 15 s, menu Xiaozhi i obsługę mikrofonu.

## 0.1.7 — parowanie Xiaozhi
Dedykowany przycisk Xiaozhi w głównym pasku, automatyczne otwarcie panelu po otrzymaniu nowego kodu od serwera, większy kod, MAC Wi-Fi, ponowienie przez restart bez kasowania NVS. Stan pokazuje faktyczny błąd połączenia oraz sytuację, gdy serwer nie dostarcza kodu/konfiguracji. Log USB pokazuje flagi odpowiedzi HTTP, bez tokenów i haseł. Urządzenie już przypisane do konta może nie otrzymać nowego kodu; odłączenie od starego agenta odbywa się w konsoli, nie przez kasowanie lokalnego UUID. Firmware nie generuje fikcyjnych kodów. Przyczyna braku kodu na konkretnym urządzeniu pozostaje do ustalenia z logów.

## 0.1.8 — identyfikacja urządzenia i konto testowe
OTA używa zweryfikowanego MAC stacji C6; ekran Xiaozhi pokazuje dokładny Device-ID wysłany do serwera. Brak prawidłowego MAC powoduje ponowienie zamiast wysyłania niezainicjalizowanych bajtów. Odpowiedź serwera z GID_test@@@ lub test-token bez aktywacji jest osobnym stanem: Espen nie uznaje jej za sparowanie i sprawdza ponownie co 30 s. Zobacz https://github.com/78/xiaozhi-esp32/issues/1785 — autor wskazuje procedurę odwiązania urządzenia https://my.feishu.cn/wiki/IJmWwu841isfAWkz1dEcrJjznof. Nie zmieniamy MAC/UUID ani nie generujemy kodu lokalnie. Wymagany log z fizycznego urządzenia, aby stwierdzić, czy serwer rzeczywiście zwraca tryb testowy.

## 0.1.9 — przejście z parowania do rozmowy
Po dodaniu urządzenia w konsoli odświeżamy konfigurację OTA co cykl zamiast dziesięciu prób /activate na tym samym challenge. Uznajemy aktywację po świeżej odpowiedzi z użyteczną konfiguracją i bez wymogu parowania albo po potwierdzeniu /activate i pobraniu użytecznej konfiguracji. Code-only jest obsługiwany przez odpytywanie OTA. MQTT/WS testowe nie blokują prawidłowego drugiego protokołu. Ustawiono timeout HTTP 10 s na operację, bez keep-alive, oraz osobne komunikaty stanu i bezpieczne logi bez payloadu aktywacyjnego. Nie zmieniamy identyfikatora, NVS, Wi-Fi ani głośnika. W portalu można wyłączyć OTA; Espen i tak nie instaluje zdalnego firmware. Test na fizycznym urządzeniu nadal konieczny.
