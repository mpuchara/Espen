# Ala Watch 0.2.12 — Garett Essa 2 AI 4G (Android 8.1)

Oddzielna aplikacja od firmware ESP32-P4. Źródła aplikacji zegarka znajdują się w gałęzi `feature/garett-espen-watch`, PR #3; nie nadpisują firmware ESP32. Wersja zegarka `0.2.12-watch`, pakiet `pl.espen.garett`, `minSdk 26`, `targetSdk 27`, podpis APK v1. Android 8.1 zegarka ma własne ograniczenia i sterowniki audio.

## Możliwości

- Pełnoekranowa wektorowa buźka Ali: mruganie, oczy i brwi, usta zsynchronizowane z poziomem PCM, emocje z `llm`, głaskanie, reakcje kontekstowe.
- **20 reakcji** z syntetyzowanymi lokalnie sygnałami PCM **bez nowego AudioTrack**: fanfary, chichot, zdziwienie, ups, sekret, zagadka, przytulenie, magia, taniec, przewrócenie oczami, zwycięstwo, eureka, napięcie, psikus, smutek, senność, detektyw, robot, werble, potakiwanie. Co najwyżej jedna reakcja na wypowiedzianą odpowiedź. Efekty miksowane bezpośrednio do TTS; to nie jest 37 efektów sprzętowego firmware.
- Xiaozhi przez OTA / WebSocket / Opus z osobnym Device-ID / Client-ID; serwerowy polski agent i emocje. Mikrofon `MIC` 16 kHz, wyjście `STREAM_MUSIC` z routingiem głośnika. Domyślna głośność multimediów: **75% przy pierwszym uruchomieniu**.
- **Aparat + analiza**: na wyraźną prośbę użytkownika, w tym „co widzisz?” i „zrób zdjęcie”. Analiza Xiaozhi Vision domyślnie włączona; nadal wymaga serwerowego HTTPS Vision. Po wykonaniu zdjęcia pojawia się tymczasowa miniatura na buźce (RAM, nie plik).
- **Bez odbioru**: przesuń palcem z góry w dół na twarzy albo wypowiedz „bez odbioru” podczas aktywnego słuchania Xiaozhi. Kończy sesję i blokuje automatyczne rozmowy. Dotknięcie budzi Alę i rozpoczyna rozmowę.
- **Lokalne, uczone hasło**: Ustawienia → Naucz hasła. Powtórz hasło w **trzech osobnych próbach**. W stanie „bez odbioru” rozpoznawanie korzysta z lokalnego porównywania cech krótkich próbek (DTW); żadne nagrania nie idą do serwera. Wymaga uprawnienia mikrofonu i otwartej aplikacji na ekranie. Jest **eksperymentalne**: może mieć fałszywe aktywacje lub nie rozpoznawać hasła, szczególnie w hałasie.
- **Przerywanie**: dotknij mikrofonu podczas TTS, aby natychmiast zatrzymać poprzednią sesję i rozpocząć nową po zwolnieniu AudioTrack. Opcjonalne „przerywanie głosem” próbuje monitorować mikrofon równolegle z TTS, ale jest **domyślnie wyłączone** z powodu awarii HAL wcześniejszych wersji. Po pierwszym niepowodzeniu tej sesji automatycznie rezygnuje z kolejnych prób. Brak gwarancji działania ani 400 ms pre-roll na tym sprzęcie.

## Sterowanie zegarkiem

Przesuń twarz **w górę**, aby otworzyć panel funkcji Androida, lub otwórz Ustawienia → Sterowanie zegarkiem:

- Odczyt baterii, głośności, statusu Wi-Fi i Bluetooth (jeśli Android udostępnia te dane).
- Głośność multimediów, lokalna jasność ekranu Ali (10–100%), wibracje i latarka, jeśli jest obsługiwana.
- Skróty do systemowych ustawień Wi-Fi, Bluetooth, GPS; minutnik i ekran wybierania numeru. Wykonanie połączenia oraz ustawienie minutnika pozostaje po stronie użytkownika.
- Narzędzia Xiaozhi MCP: `self.watch.get_status`, `set_volume`, `set_brightness`, `vibrate`, `set_flashlight`, `set_timer`, `open_settings`. W każdym przypadku nieobsługiwanego API otrzymuje odpowiedź o braku dostępności.

Aplikacja **nie** ma roota i nie obchodzi ograniczeń Garett: nie może bez uprawnień zmieniać globalnej jasności, wymuszać GPS/4G, zastępować SOS, lokalizatora rodzicielskiego ani blokować systemu. Nie odczytuje listy kontaktów i nie wysyła SMS-ów.

## Instalacja i build

GitHub Actions: **Build Garett Espen Android APK**, artefakt `garett-espen-android81-full-v0.2.12` (ZIP zawierający `app-debug.apk`). APK w trybie debug z kompatybilnym podpisem JAR/v1; `applicationId` jest ten sam co wcześniej, więc instalator może wykonać aktualizację, o ile podpis certyfikatem debug jest zgodny. W innym wypadku Android zgłosi konflikt podpisu i konieczna może być deinstalacja starej aplikacji, co usuwa jej lokalne dane aktywacyjne.

Wymagania: JDK 17, Android SDK 35, Gradle 8.7. W katalogu `android/garett-espen`:

```bash
gradle --no-daemon :app:testDebugUnitTest :app:assembleDebug
```

W repo jest też lekka aplikacja diagnostyczna w module `lite`, której można użyć do sprawdzenia instalatora zegarka. Instalację APK wykonaj sprawdzoną metodą przesłania pliku na zegarek.

## Ważne ograniczenia i prywatność

- To build developerski, **wymaga testów na fizycznym Garett Essa 2**. W szczególności nie deklarujemy potwierdzonego wybudzania i przerwania głosem.
- Nie działa jako usługa w tle; wygaszenie lub zamknięcie aplikacji może wyłączyć lokalne hasło, zgodnie z ograniczeniami Androida i oszczędzaniem baterii.
- W aktywnej sesji AI dźwięk płynie do Xiaozhi. Tryb lokalnego wybudzania czyta mikrofon tylko do porównania wzorca na urządzeniu; w stanie „bez odbioru” po włączeniu tej opcji i trzech próbkach mikrofon może nadal pracować lokalnie.
- Zdjęcie jest w RAM; wysyłka wyłącznie do HTTPS Vision dostarczonego przez Xiaozhi, po rozpoznaniu świadomej prośby użytkownika.
- Przycisk „Diagnostyka” kopiuje logi bez zdjęć, audio i tokenów.
- Gdy Xiaozhi udostępnia tylko MQTT, a nie WebSocket, ta aplikacja nie ustanowi rozmowy.
