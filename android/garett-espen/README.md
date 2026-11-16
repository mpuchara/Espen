# Ala / Espen dla Garett Essa 2 AI 4G

Eksperymentalna, **natywna aplikacja Android 8.1** na Garett Essa 2 AI 4G. Osobny projekt; nie zmienia firmware Espen P4. Bazuje na logice emocji, gestów i prywatności z `firmware/face_engine.h`, `social_reactions.h`, `vision_gate.h`. Animacje rysuje wektorowo przez Android Canvas (bez kopiowania C++/LVGL).

## Co jest w MVP
- Pełnoekranowa buźka Ala (mruganie, spojrzenia, ruch ust przy TTS, emocje z wiadomości `llm`, dotyk: mrugnięcie/głaskanie/zaskoczenie).
- Tap w przycisk mikrofonu: sesja Xiaozhi OTA → WebSocket → Opus 16 kHz / 60 ms → TTS Opus 24 kHz, transkrypcja oraz napisy.
- Aktywacja nowego urządzenia Xiaozhi: własne stabilne Device-Id i Client-Id, kod na ekranie, odświeżanie OTA i próba /activate. Sparuj zegarek **osobno**, w konsoli z istniejącym agentem Ala.
- Zdjęcia: obsługa MCP `self.camera.take_photo`, serwerowego `vision.url`, HTTPS multipart, tylko po włączeniu opcji i jawnym pytaniu użytkownika. Zdjęcie pozostaje w pamięci RAM, nie jest zapisywane na dysku. Rozumienie obrazu zależy od obsługi Vision po stronie serwera Xiaozhi.
- Konfiguracja OTA z poziomu zegarka. Bez Google Play Services, bez zależności od Garett Tracker w sesji AI.

## Budowa i instalacja
Otwórz folder `android/garett-espen` w Android Studio, lub uruchom:
```bash
gradle assembleDebug
```
Android SDK 35, Gradle 8.7, JDK 17. Debug APK: `app/build/outputs/apk/debug/app-debug.apk`. W GitHub Actions: workflow **Build Garett Espen Android APK**, artefakt `garett-espen-debug-apk`.

Przenieś plik APK na zegarek tą samą metodą Bluetooth → otrzymany plik → instalator, którą udało się zainstalować `pl.garett.hello`. Pakiet jest inny: `pl.espen.garett`.

## Obsługa
- Ekran: przycisk mikrofonu na dole rozpoczyna i kończy rozmowę; mikrofon może poprosić o uprawnienie.
- Dotykaj/głaszcz buźkę. Przytrzymaj ok. 1,5 s aby otworzyć ustawienia.
- Po pierwszym starcie pojawi się kod. W Xiaozhi Console → istniejący agent Ala → Add Device wpisz ten kod. Nie wklejaj tam tokenów.
- W ustawieniach domyślny OTA: `https://api.tenclass.net/xiaozhi/ota/`.
- Aby analizować obraz, świadomie włącz **Zdjęcia dla AI**. Powiedz np. „Ala, co widzisz?” i poczekaj, aż Xiaozhi wywoła narzędzie kamery.

## Bezpieczeństwo i ograniczenia
- To wersja developerska; brak testów na fizycznym Essa 2. Poprawność pracy audio/wydajność, kamera legacy, odpowiedzi OTA konkretnego konta i kompatybilność MCP wymagają testów.
- Bez stale aktywnego mikrofonu / hotword „Hej Ala”. Przy aktywnej sesji mikrofon wysyła dźwięk do Xiaozhi, a po końcu wypowiedzi kończy wysyłanie. W tle i przy zgaszonym ekranie aplikacja nie rejestruje.
- Serwer musi dostarczyć **WebSocket** w odpowiedzi OTA. Jeśli zwraca tylko MQTT, ta wersja pokaże błąd; nie podstawiamy fikcyjnego endpointu.
- Zdjęcie jest wysyłane wyłącznie HTTPS do URL podanego przez backend podczas MCP. Brak takiego URL = brak wysyłki. Użytkownik może wyłączyć zdjęcia w ustawieniach.
- Aplikacja nie ingeruje w SOS, połączenia, GPS ani funkcje rodzicielskie zegarka. Ma własny identyfikator Xiaozhi; nie podszywa się pod ESP32.
- Przewidywany większy pobór baterii podczas aktywnej rozmowy i animacji. Zadbaj o aktualizację zależności przed dalszą dystrybucją.
