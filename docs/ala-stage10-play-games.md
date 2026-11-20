# Ala Stage 10 — gry dotykowe
**Repo:** mpuchara/Espen; bazowy PR #47. Trzy gry offline dla dzieci (bez HA, API, STT/TTS, nowego serwera).
## Sterowanie
Dotknij **GRY** na twarzy Ali lub **Ustawienia → Gry dla dzieci**. Trzy duże przyciski, X (wyjście), Dalej, Zagraj ponownie i Inna gra. Tekst po polsku z pełnym fontem. UI i stan gier działają na LVGL, niezależnie od audio.
## Gry
- **Quiz Mistrza:** 12 pytań z wiedzy ogólnej, pięć różnych pytań na rozgrywkę, trzy możliwości, 10 gwiazdek za poprawną odpowiedź.
- **Zgadnij zwierzę:** dziewięć zagadek przyrodniczych, pięć w rozgrywce, trzy możliwości, punkty i zachęcające komunikaty.
- **Magiczna opowieść:** trzy decyzje, po dwie opcje, wybory wpływają na dalszą scenę i jedno z czterech zakończeń.
## Prywatność i ograniczenia
Rozgrywka w RAM, bez kamery, mikrofonu, Internetu, danych osobowych ani NVS. Gry nie przerywają strumienia audio Xiaozhi. Nie ma jeszcze automatycznego mówienia instrukcji gier ani głosowego wybierania odpowiedzi; interakcja odbywa się dotykiem. Nie dodano LLM ani nowego TTS/STT.
## Testy
Host: test_ala_play_games.cc, test_ala_play_games_contracts.py. Firmware: ESP32-P4 + pełny espen.bin SHA256. 
**WYMAGANE na fizycznej Ali:** sprawdzić przycisk GRY, odpowiedzi, punkty, wszystkie zakończenia historii, polski font, działanie X i powrotu, 30 min rozmów Xiaozhi podczas uruchamiania/zamykania gier, dotyk, standby, barge-in i STOP. Nie oznaczać wersji jako stable bez testów urządzenia.
