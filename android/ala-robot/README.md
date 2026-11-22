# Ala Robot — dwie natywne aplikacje Android

Wspólny kod dla **telefonu Android** (pakiet `pl.homemind.ala.robot`) i **Garett Kids Essa 2 AI 4G** (pakiet `pl.espen.garett`, Android 8.1). To **robot Ala działający samodzielnie na urządzeniu**, a nie pilot ESP32-P4. Wcześniejszy `android/app` to osobny Pilot Ali i pozostaje bez zmian.

Wersja: 0.3.0-beta1, port funkcji z rozwoju ESP32-P4 do **Ala 0.2.28 / PR #56**. Nie jest to binarny port firmware C++ / LVGL. Współdzielony kod animacji i połączenia Xiaozhi dla obu ekranów ma własne identyfikatory urządzeń. Rejestracja w *tym samym agencie Ala* w Xiaozhi jest osobna dla P4, telefonu i zegarka.

## Działa w kodzie

- Animowana twarz z emocjami Xiaozhi, mruganiem, mimiką i poziomem audio; gesty i dotyk.
- Własna aktywacja Xiaozhi (OTA + WebSocket protokół 1 + Opus), mikrofon 16 kHz i odtwarzanie głosu. Kod aktywacji pojawia się na twarzy.
- Aparat tylko na żądanie, za zgodą w ustawieniach; zdjęcie w RAM, HTTPS Vision MCP, jeśli backend udostępnia.
- Pasek: Gry, Aparat, Mikrofon. Długie przytrzymanie twarzy otwiera ustawienia.
- Menu gier, quiz, rozpoznawanie zwierząt, trzy tematy Magicznej Opowieści; opowieść prowadzona głosowo przez Xiaozhi i z twarzą nadal na ekranie. Głosowy pomysł dziecka przekazywany jako kontekst do kontynuacji; bezpieczna maksymalna długość historii.

## Ograniczenia (nie ukrywać)

- **Brak pełnej parytetowości z firmware P4:** modele lokalnego rozpoznawania twarzy, pamięci i budzika/hotword ESP-SR, autonomii społecznej oraz zaawansowanej synchronizacji audio z P4 nie są przenoszone 1:1. Android nie uruchamia ESP-IDF.
- Hasło „Hej Ala” nie działa w tle. Mikrofon działa **wyłącznie po świadomym dotknięciu** i w aktywnej sesji; nagrywanie zatrzymuje się po przejściu do innej aplikacji.
- Lokalny głos menu/quizów korzysta z Android TextToSpeech (o ile dostępny polski silnik). Historia korzysta z mowy Xiaozhi i wymaga sieci. Gdy backend Xiaozhi nie obsługuje wysłania tekstu `listen.detect`, opowieść nie wystartuje — sprawdzić na właściwym agencie.
- Garett **Essa 4G / Essa GO / Essa 2** mogą mieć różne wersje zablokowanego Androida i instalatora. Starsza aplikacja testowa była skierowana do **Essa 2 AI 4G**; brak gwarancji sideloadu na pozostałych. Nie ingerować w SOS/GPS/Garett Tracker.
- Nie wolno kopiować parowania, identyfikatora ani tokenu ESP32 na telefon/zegarek. Każde urządzenie ma własne, lokalnie zapisane UUID i Device-ID.
- Kod jest kandydatem do testów: nie stwierdzamy, że działa na fizycznym Garett lub telefonie, dopóki nie przeszły testy urządzeń. APK z CI jest podpisana kluczem debug.

## Budowa

Z katalogu `android/ala-robot`:

```sh
gradle :watch:testDebugUnitTest :watch:assembleDebug :phone:assembleDebug
```

Artefakty w GitHub Actions > **Ala Robot — Android phone and Garett Essa**: `Ala-Robot-Android-Phone-APK` i `Ala-Garett-Essa-Android81-APK`. **Zegarek** dostaje APK z podpisem v1, bez konieczności Google Play; **telefon** normalne APK Android 8+. Instalacja na Garett tylko jeśli oprogramowanie producenta na nią pozwala.

Po uruchomieniu: w **Xiaozhi Console** dodaj nowe urządzenie do istniejącego agenta Ala, wpisując kod widoczny w aplikacji. W ustawieniach: test mikrofonu i głośnika, diagnostyka, zgoda na aparat. Bez Internetu nie będzie głosu AI.

## Testy akceptacyjne

1. Instalacja obydwu APK, osobna aktywacja i sprawdzenie Device-ID/Client-ID; po restarcie identyfikatory nie zmieniają się.
2. 10 rozmów, 10 przełączeń pomiędzy mikrofonem i bezczynnością; wyjście do innej aplikacji zatrzymuje nagrywanie.
3. 5 odsłuchanych odpowiedzi: dźwięk w głośniku, synchronizacja animacji ust, brak echo/bootloop.
4. Sprawdzenie paska 3 ikon na obu rozmiarach, możliwość powrotu z każdej gry, quiz i zgadywanie zwierząt.
5. Magiczna Opowieść: 3 tematy, pełny fragment głosowy z animowaną twarzą, wybór propozycji lub nagranie własnego pomysłu, spójna kontynuacja.
6. Aparat nie wysyła zdjęcia bez pytania i zgody; sprawdzić na Xiaomi/Garett wymuszenie HTTPS i sesje bez backendu Vision.
7. Zegarek: kontrola baterii/temperatury oraz nienaruszone SOS, telefon, lokalizacja i Garett Tracker.
