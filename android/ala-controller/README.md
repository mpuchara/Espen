# Ala Panel — APK na telefon Android

Osobna aplikacja telefoniczna do **lokalnego podglądu emocji Ali**. Nie zmienia firmware Espen ani aplikacji zegarka.

## Funkcje działające bez połączenia
- Interaktywna twarz i osiem emocji: neutralna, radość, miłość, nieśmiałość, smutek, zdziwienie, złość, sen.
- Rumieńce; włączanie na stałe lub na czas reakcji. **X** kończy reakcję.
- Opcjonalne opóźnienie 5 s, czas reakcji regulowany 1–120 s (domyślnie 47 s).
- Stan i ustawienia są widoczne na telefonie, podstawowe preferencje zapisują się lokalnie.

## Ważne: sterowanie prawdziwą Alą
Firmware Espen 0.2.11/0.2.12 **nie udostępnia ogólnodostępnego interfejsu HTTP do zdalnych emocji**, więc sam APK nie może wymusić miny na robocie. Nie twierdzimy, że aplikacja jest połączona bez serwera pośredniczącego. Opcjonalny ekran „Połączenie” przyjmuje URL kompatybilnego serwera pośredniczącego. Tylko po samodzielnym wprowadzeniu adresu wysyła polecenia:
- `GET {base}/api/ala/status` — test połączenia (HTTP 2xx).
- `POST {base}/api/ala/emotion` z JSON: `{"emotion":"love","blush":true,"permanent":true,"duration_seconds":47,"delay_seconds":5}`.
- Opcjonalny nagłówek `X-Ala-Token`, jeżeli serwer go wymaga.
- HTTP jest dopuszczalne **tylko dla prywatnych adresów LAN** (10.*, 192.168.*, 172.16–31.*, 127.0.0.1). Pozostałe adresy wymagają HTTPS.

Aplikacja działa również bez żadnego serwera; wtedy przyciski sterują wyłącznie podglądem na telefonie. Nie zapisujemy zdjęć ani dźwięków, nie używamy mikrofonu, kamery ani uprawnień lokalizacji.

## Budowanie
JDK 17 / Android SDK 35 / Gradle 8.7. W katalogu `android/ala-controller`:
```sh
gradle --no-daemon :app:assembleDebug
```
Wynik: `app/build/outputs/apk/debug/app-debug.apk`.
