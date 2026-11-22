# Ala Robot na Androidzie i Garett Essa

Wersja kandydująca **0.3.1-beta1**. Telefon (`pl.homemind.ala.robot`) i Garett Essa 2 AI 4G (`pl.espen.garett`) kompilują tę samą logikę zachowania, gier i komunikacji Xiaozhi. Każde urządzenie paruje się osobno z agentem Ala. Wzorzec zachowania pochodzi z firmware’u [0.2.28, PR #56](https://github.com/mpuchara/Espen/pull/56).

- Gry i rozmowa są dostępne z dolnego paska. Magiczna Opowieść używa głosu Xiaozhi, a pytania Quizu Mistrza i Zgadnij zwierzę czyta lokalny polski syntezator Androida, jeśli jest dostępny.
- Dotknięcie mikrofonu w trakcie nagrywania kończy wypowiedź. Dotknięcie podczas przygotowywania lub odtwarzania odpowiedzi anuluje turę. Mikrofon jest zatrzymywany po opuszczeniu ekranu Ali.
- Zdjęcia do analizy są domyślnie wyłączone. Po włączeniu analiza wymaga jawnej czynności użytkownika i dostępnego HTTPS Vision. Zdjęcie istnieje tymczasowo w RAM. Po anulowaniu zgoda i oczekujące zdjęcie są unieważniane.
- Tryb debug obu APK ma podpis testowy. Zegarek używa podpisu v1 i target API 27, telefon target API 35. Oba pakiety mają min API 26. Android wymaga tego samego podpisu przy aktualizacji istniejącego pakietu. Lokalne 0.3.1-beta1 i dostarczone wcześniej lokalne 0.3.0-beta2 używają tego samego testowego certyfikatu. Publiczne 0.3.0-beta1 ma inny certyfikat.

## Budowa i weryfikacja

W katalogu `android/ala-robot`, przy użyciu JDK 17 i Gradle 8.10.2:

```sh
gradle :watch:testDebugUnitTest :watch:assembleDebug :phone:assembleDebug
gradle :watch:connectedIntegrationAndroidTest :phone:connectedDebugAndroidTest
```

Testy integracyjne uruchamiają lokalny serwer HTTPS/WebSocket za pomocą MockWebServer i korzystają z Android `AudioTrack`, `AudioRecord` i aparatu. Wariant `integration` używa tego samego kodu aplikacji i zachowuje biblioteki potrzebne przez testy instrumentacyjne. Artefakty kandydujące z Actions mają nazwy `Ala-Robot-Android.apk` i `Ala-Garett-Essa-Android81.apk`.

## Sprawdzenie na urządzeniach

1. Zainstalować wariant telefonu i wariant zegarka. Zweryfikować aktualizację bez utraty konfiguracji przy zachowanym podpisie i parowaniu.
2. W Xiaozhi Console przypisać oba urządzenia do agenta Ala. Każde urządzenie ma własne Device-ID i Client-ID.
3. Sprawdzić dziesięć kolejnych rozmów, odpowiedzi równocześnie z mimiką, szybkie przerwanie, utratę Internetu, powrót, tło i fizyczny przycisk wygaszania zegarka.
4. Przejść Quiz Mistrza, Zgadnij zwierzę i wszystkie tematy Magicznej Opowieści. Sprawdzić odsłuch wprowadzenia, wybór tematu, akceptację propozycji, podyktowanie pomysłu i spójność zakończenia.
5. Sprawdzić aparat przy wyłączonej zgodzie, po anulowaniu zdjęcia i z właściwym backendem Vision. Pomiar CPU, RAM, baterii i płynności wykonać na fizycznym Garett.
6. Zweryfikować działanie funkcji producenta, w tym SOS, GPS i połączeń.

[Stabilizacja i weryfikacja 0.3.1](../../docs/ala-0.3.1-validation.md) · [Audyt i macierz zgodności](../../docs/ala-cross-platform-audit.md)
