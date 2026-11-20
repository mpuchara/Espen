# Ala Stage 10 — aktualizacja gier Ali i nawigacji

**Gałąź:** `feature/ala-play-games-stage10` (PR #50). Funkcja dziecięca, bez integracji Home Assistant.

## Magiczna opowieść
Wybór tematu na ekranie: **W magicznym lesie**, **Nad jeziorem** oraz **Z mamą w samochodzie**. Każdy temat ma trzy etapy decyzji, dwa warianty na etap i cztery zakończenia. Scena i dalsze możliwości pojawiają się w kartach blisko dolnej części ekranu.

**Głos:** po wyświetleniu sceny Ala prosi już skonfigurowanego asystenta Xiaozhi, aby przeczytał tekst historii i kolejno nazwał opcje (pierwsza, druga, trzecia). Przycisk **Powtórz opcje** pozwala ponowić narrację aktualnej sceny, również przy zakończeniu. Nowy tekst jest kolejkowany, aż urządzenie będzie gotowe; nie rozbrzmiewają jednocześnie dwa wywołania. **To nie jest lokalny TTS:** bez Wi-Fi i działającego Xiaozhi historia oraz odpowiedzi nadal działają na dotyk, lecz narracja głosowa nie jest dostępna. Ostateczny sposób odczytania tekstu zależy od odpowiedzi chmury Xiaozhi.

## Stały dolny pasek
Trzy duże ikony **Gry**, **Aparat**, **Mikrofon** zostały przeniesione na dół ekranu pod przewijaną rozmowę. Pasek jest niezależny od twarzy robota, a stary przycisk GRY z twarzy usunięty. Aparat otwiera dotychczasowy widok kamery; Mikrofon wywołuje dotychczasowe ręczne start/stop nasłuchiwania, a w stanie bez odbioru służy do wybudzenia. Bez nowych sterowników, torów audio i zadań nagrywania. Pasek jest nad warstwą modalną; pamiętaj o przewijaniu ustawień do ostatnich elementów.

## Testy
- Host: `tests/test_ala_play_games.cc` — trzy tematy i ścieżki wyboru.
- Kontrakty: `tests/test_ala_play_games_contracts.py`.
- ESP-IDF: pełny build ESP32-P4 + walidacja obrazu instalacyjnego przed publikacją.
- Na fizycznej Ali: sprawdzić trzy przyciski dolne i brak starego na twarzy, oba mikrofonowe stany, aparat, poziom/głośność narracji, odtwarzanie opcji i ich powtarzanie, zachowanie w trakcie mowy, przejścia pomiędzy etapami, offline, standby, dotyk w ustawieniach oraz normalne rozmowy Xiaozhi. **Nie oznaczać wersji jako stabilnej bez testu na urządzeniu.**
