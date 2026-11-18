# Ala Phone — panel emocji na Androida

**Osobna aplikacja APK na telefon**, bez zmian firmware Espen P4 i bez konieczności aktualizowania aplikacji Garett Essa 2.

## Obsługa
1. Zainstaluj `Ala-Phone-Panel.apk` na telefonie i uruchom **Ala Phone**.
2. Na dole ekranu wybierz **🎭 PANEL EMOCJI**.
3. Dotknij emocji. Po **5 sekundach** pojawi się na buźce Ali **w tej aplikacji**.
4. **Zakochanie** albo **Rumieńce** pozostają na stałe, dopóki nie dotkniesz **✕ Usuń emocję i rumieńce**.
5. **🎙 MÓW** otwiera rozmowę Xiaozhi z osobnym identyfikatorem telefonu (wymaga aktywacji w swoim koncie Xiaozhi); **📷** robi zdjęcie na wyraźne polecenie i zgodę Androida.
6. **⚙** to ustawienia Ali: audio, OTA, przyzwolenie na Vision i diagnostyka. W panelu emocji dostępny jest przycisk **Sterowanie tym telefonem** (jasność tylko okna Ali, głośność multimediów, wibracja, obsługiwana latarka, skróty do Wi-Fi/Bluetooth/GPS).

## Ważne: zakres sterowania

Panel steruje **buźką Ali wyświetlaną w APK na telefonie**, a nie robotem ESP32-P4 ani zegarkiem. Nie istnieje tu automatyczne bezprzewodowe połączenie z nimi, więc nie obiecujemy zdalnego zmieniania mimiki na zewnętrznym urządzeniu. Robot i zegarek nie zostały zmodyfikowane. Komunikacja Xiaozhi na telefonie to osobna sesja AI.

Nie wymaga instalacji dodatkowych aplikacji na zegarku, nie zmienia SOS, nie uruchamia mikrofonu w tle. Tryb lokalnego wybudzania jest opcjonalny i nie działa przy wygaszonej/zamkniętej aplikacji.

## Build APK
- Workflow: **Build Ala Phone Android APK** (GitHub Actions).
- Artefakt `ala-phone-android-apk` zawiera `app-debug.apk`.
- Lokalnie: JDK 17, Gradle 8.7, Android SDK 35:
```sh
cd android/ala-phone
gradle --no-daemon :app:testDebugUnitTest :app:assembleDebug
```
- APK debug podpisane kluczem Android debug (do testów poza Google Play). Osobny pakiet `pl.espen.ala.phone`, więc nie zastępuje aplikacji na zegarek.

## Zgodność
- Android API 26+ (8.0+). Telefon Android 17 może wymagać potwierdzenia instalacji z nieznanych źródeł.
- Konfiguracja Xiaozhi WebSocket potrzebna do rozmowy; sam panel mimiki działa całkowicie offline.
- Nie przetestowano na fizycznym telefonie; kompilacja CI jest odrębnym krokiem.
