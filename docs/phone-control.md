# Pilot Ali na telefon (eksperymentalny)

Panel jest częścią firmware ESP32-P4 w repozytorium Espen. **Nie jest to APK ani kontrolka na zegarku**. Po wgraniu nowego firmware stronę otwiera się bezpośrednio z adresu IP robota.

## Jak uruchomić

1. Wgraj firmware z gałęzi zawierającej tę funkcję. Panel nie zadziała z wcześniej wgraną wersją.
2. Podłącz Alę i telefon do **tej samej lokalnej sieci Wi-Fi**.
3. Na Ali otwórz **Ustawienia → Pilot telefonu / kod**. Na osobnym, czytelnym ekranie znajdziesz **Adres IP** oraz duży **8-cyfrowy kod pilota**. Nie pomyl go z kodem aktywacji Xiaozhi. Kod powstaje automatycznie przy pierwszym użyciu, jest zapisany w NVS i pozostaje taki sam po zwykłej aktualizacji. Jeśli nie ma Wi-Fi, Ala pokaże komunikat zamiast fikcyjnego IP.
4. W aplikacji **Pilot Ali na Androida** wpisz dokładnie podany IP i 8 cyfr. Możesz też otworzyć w Chrome adres `http://<IP>/` (dokładnie adres wyświetlony przez Alę, nie adres GitHub Pages). Możesz dodać stronę do ekranu głównego.
5. Wpisz ośmiocyfrowy kod i dotknij **Połącz**. Kod jest unikatowy dla danego urządzenia i zapisywany w jego NVS; aktualizacja aplikacji nie powinna go skasować.
6. Wybierz emocję: za 5 sekund pojawi się na twarzy Ali. Domyślnie zostaje aż do kliknięcia **X Wyłącz emocję**; możesz odznaczyć opcję „Na stałe”.
7. Wpisz zdanie, użyj **Wstępnie popraw**, a potem **Niech Ala powie**. Robot poprosi tego samego agenta Xiaozhi o językową korektę zdania i wypowiedzenie samej poprawionej wersji.

## Granice działania

- Wstępna korekta na telefonie obejmuje tylko odstępy, wielką literę i podstawową interpunkcję. Korektę językową i polski głos realizuje agent Xiaozhi, nie lokalna przeglądarka.
- Wpisany tekst jest wysyłany przez **lokalne HTTP** do robota, a następnie przez Xiaozhi do zewnętrznego serwera. Nie wpisuj prywatnych informacji. Połączenie LAN nie jest szyfrowane, więc nie używaj niezaufanej sieci.
- Tekst→AI→TTS wykorzystuje komunikat `listen.detect` i wymaga testu na sprzęcie z serwerem Xiaozhi; nie ma gwarancji, że odpowiedź zachowa dokładnie wpisane słowa. Przy zajętym urządzeniu polecenie jest odrzucane.
- Nie ma dostępu do urządzenia spoza lokalnej sieci, nie ma automatycznego odkrywania IP i nie ma sterowania z GitHub Pages (przeglądarki blokują HTTPS→HTTP).
- Serwer HTTP udostępnia tylko stronę panelu i cztery chronione kodem endpointy. Bez kodu nie można wysłać polecenia ani odczytać odpowiedzi.
- Nie drukuj kodu w logach ani nie udostępniaj go innym osobom. Panel ogranicza długość tekstu i odrzuca kontrolne znaki wejścia.

## Test po aktualizacji

- Połącz telefon z Alą; zły kod ma zwracać `401`.
- Wyślij „Zakochanie” na stałe, odczekaj 5 sekund i sprawdź serca; naciśnij X i sprawdź reset. Powtórz z „Rumieńce”.
- Odznacz „Na stałe”, wybierz „Radość”; animacja powinna wrócić po około 4 sek. 
- Wpisz „czesc  ala jak  sie masz” i sprawdź wstępne uporządkowanie oraz odpowiedź i dźwięk Xiaozhi; podczas mówienia drugie polecenie ma być odrzucone.
- Odłącz Wi-Fi i sprawdź obsługę błędu w telefonie oraz zwykłe rozmowy głosowe na robocie po ponownym połączeniu.

Pełny test automatyczny firmware wymaga obrazu ESP-IDF w GitHub Actions. Test `tests/test_phone_panel.py` weryfikuje integrację i poprawność patchy, ale **nie zastępuje testu audio na fizycznej Ali**.
