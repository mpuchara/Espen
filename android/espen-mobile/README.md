# Espen Mobile — Android MVP 0.1.0

Pierwsza aplikacja na telefon w ramach projektu Espen. Android 8.0+ (API 26+). Nie zmienia firmware ESP32-P4. To **samodzielny prototyp głosowego interfejsu**, jeszcze bez połączenia z agentem Xiaozhi.

## Funkcje
- Animowana buźka i emocje: czuwanie, słuchanie, przetwarzanie, mówienie, uśpienie.
- Stuknięcie buźki lub przycisku **Mów** uruchamia systemowe rozpoznawanie mowy po polsku.
- Systemowy TTS czyta odpowiedź na głos. Historia wypowiedzi jest widoczna na ekranie.
- Proste lokalne odpowiedzi: powitanie, żarty, godzina, podstawowe dodawanie/odejmowanie/mnożenie, pytanie o możliwości.
- Komenda **bez odbioru** albo przesunięcie po buźce w dół wycisza robota; dotknięcie go wybudza.
- Przycisk **Przerwij** zatrzymuje mowę i nasłuchiwanie.
- Brak pracy mikrofonu w tle. Mikrofon jest uruchamiany tylko po działaniu użytkownika; dostęp wymaga uprawnienia Androida.

## Instalacja
Na karcie **Actions** repo wybierz workflow **Espen Mobile APK**, a po udanym uruchomieniu pobierz artefakt **espen-mobile-debug-apk**. Rozpakuj ZIP i zainstaluj `app-debug.apk` na telefonie. Android może poprosić o zgodę na instalację z wybranego źródła.

## Budowanie lokalne
Android Studio z JDK 17, Android SDK 35 i Gradle 8.9:

```sh
cd android/espen-mobile
gradle assembleDebug
```

APK powstaje w `app/build/outputs/apk/debug/app-debug.apk`.

## Ważne ograniczenia
Ten MVP używa systemowych `SpeechRecognizer` i `TextToSpeech`. Działanie rozpoznawania i polskiego głosu zależy od zainstalowanych usług w telefonie i może wymagać Internetu. Odpowiedzi powstają lokalnie według prostych reguł — to **nie jest jeszcze rozmowa z Xiaozhi ani generatywną AI**. Kolejnym etapem jest oddzielna, bezpieczna integracja protokołu Xiaozhi z aktywacją urządzenia, obsługą Opus i prawidłową wymianą audio. Nie kopiować tożsamości sprzętowej istniejącego ESP32 ani tokenów z firmware.
