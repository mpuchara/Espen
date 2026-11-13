# Espen — polski asystent głosowy na ESP32-P4

Pierwszy port [Xiaozhi ESP32](https://github.com/78/xiaozhi-esp32) v2.5.2 dla GUITION JC4880P443C_I_W(_Y), z ekranem ST7701S 480×800, GT911, mikrofonem ES8311 i Wi-Fi przez ESP32-C6. Niezależny od Home-Buddy. Wersja 0.1.0 jest eksperymentalna; wymaga testu na sprzęcie.

## Instalacja
[Instalator Espen](https://mpuchara.github.io/Espen/) — Chrome/Edge na komputerze, kabel USB z danymi. Wybierz Install i port **P4**, nie C6. Jeśli potrzebne, przytrzymaj BOOT podczas resetu i ponów połączenie. Pierwsza instalacja zastępuje poprzedni firmware i może skasować konfigurację; wykonaj kopię. Instalator obsługuje tylko wariant P4 rev1 (16 MB flash) tej konkretnej płytki, nie inne P4.

Po starcie połącz się z siecią konfiguracyjną Xiaozhi i wprowadź Wi-Fi. Na ekranie pojawi się kod aktywacji. W [Xiaozhi Console](https://xiaozhi.me/console/agents) wybierz agenta Ala → Devices/Add Device i wpisz kod. Nie wpisuj haseł ani kodów do GitHuba. Nie trzeba zakładać kolejnego agenta.

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
