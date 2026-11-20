# Ala — Etap 5: Social Intelligence Engine (bezpieczny zakres eksperymentalny)

**Baza:** PR #37 (Etap 4), na gałęzi `feature/ala-conversation-intelligence-stage4`, zależnej od PR #36 i #35. Testy z PR #34 są przeniesione przez #35; PR #32 pozostaje osobną zmianą audio. Żadnych automatycznych merge'ów. Firmware nadal 0.2.17, instalacja tylko pełnego `espen.bin`.

## Co jest rzeczywiście zaimplementowane

- `firmware/ala_social_intelligence.h` — lekki, deterministyczny arbiter zgody i limitów społecznych; nie przechwytuje dźwięku, nie posiada Xiaozhi, LVGL ani sprzętu.
- Domyślnie wyłączony Tryb Towarzyski. Aktywacja świadomie na ekranie Ali albo przez **autoryzowany PIN-em** panel WWW. Trwa maksymalnie 10 minut; STOP, „bez odbioru”, brak Wi-Fi i restart go wyłączają.
- Wskaźnik odliczania na twarzy; ekran ustawień i panel WWW informują o możliwym przekazywaniu dźwięku do zewnętrznej usługi.
- Kontekst tworzy maksymalnie pięć ostatnich transkrypcji **wyłącznie wtedy, gdy użytkownik normalnie używa aktywnej sesji Xiaozhi i wyraził zgodę na ten tryb**. Wypowiedzi pozostają w RAM, nie w NVS ani logach, są obcinane do 160 bajtów/sztukę i usuwane przy wyłączeniu.
- Model nie może sterować Alą przez tekst cytowany w kontekście: model otrzymuje go jako dane; znaki niebezpieczne dla tekstowego protokołu `SendWakeWordDetected` są usuwane. Nadal zachowuj ostrożność — ochrona promptów nie jest gwarancją bezpieczeństwa zachowania LLM.
- Przycisk **Skomentuj teraz** jest rzeczywisty. Weryfikuje zgłoszenie z autoryzowanego panelu, trwający Tryb Towarzyski, niepusty kontekst, stan idle i połączenie, po czym korzysta z dotychczasowego `Application::EspenAskText()` → `SendWakeWordDetected()` → Xiaozhi → istniejący strumień TTS. Prosi model o krótką, dopasowaną wypowiedź, w poważnym kontekście bez żartu.
- Trzy poziomy aktywności i bramki dla **przyszłych** werdyktów semantycznych: pewność ≥80%, brak wrażliwego tematu, 1300 ms ciszy, brak TTS, minimalnie 60/90/180 s między spontanicznymi wypowiedziami, do 3 na 10 min, kontrola generacji i wersji kontekstu. `BehaviorEngine` dopuszcza spontaniczną mowę tylko z takim uprawnieniem; na publicznym Xiaozhi ta bramka pozostaje zamknięta.
- Bez zmian driverów kamery, GPIO, I2S, Wi-Fi, AEC i NSNet2. Bez nowych tasków.

## Krytyczne ograniczenie protokołu Xiaozhi

Aktualne publiczne API w repo/overlay dostarcza transkrypcję `stt` wewnątrz aktywnej rozmowy, a dotychczasowy serwer przygotowuje **własną odpowiedź TTS** na wypowiedź użytkownika. Nie potwierdzono tu serwerowego trybu „transkrybuj i oceń, ale nie mów”, generacji semantycznego werdyktu w osobnym kanale, ani możliwości anulowania wszystkich opóźnionych pakietów jednej odpowiedzi po jej wysłaniu. MCP w obecnym kodzie dotyczy dostępnych narzędzi/capabilities, nie stanowi sprawdzonego interfejsu do cichego STT.

Dlatego obecna Ala **nie analizuje w tle rozmów przy wyłączonej sesji Xiaozhi, nie podsłuchuje i nie generuje automatycznych ripost**. Włączony tryb pozwala jedynie gromadzić ograniczony kontekst z regularnej rozmowy Xiaozhi i świadomie poprosić o komentarz; **nie rozwiązuje problemu TTS odpowiadającego na każde usłyszane zdanie**. Nie udajemy, że automatyczna autonomia już działa.

**Dalsza integracja:** potrzebny potwierdzony kontrakt serwera: `STT_ONLY` → `semantic_verdict({intent,sensitive,confidence,context_revision,turn_id})` → lokalne `Evaluate` → `SpeakIfApproved` z identyfikatorem i atomowym anulowaniem; TTS wyłącznie po wyraźnej decyzji. Komenda STOP musi także unieważnić zlecone już żądania/odpowiedzi, co wymaga obsługi sesji po stronie serwera/protokołu. Bez tego nie należy włączać autonomicznych komentarzy.

## Jak testować

Automatycznie (CI host): `tests/test_ala_social_intelligence.cc`, `tests/test_ala_social_contracts.py` oraz dotychczasowe testy Behavior/Living/Conversation. Testują zgodę, wygaśnięcie, reset, ograniczenie kontekstu, ramę czasową, zawieszenie Wi-Fi, wrażliwość tematu, kolejność werdyktów, twardy limit komentarzy, aktywną mowę, cooldown i rollover 32-bit. Pełny obraz w CI nadal podlega weryfikacji layoutu 16 MB i SHA256. `scripts/build.sh` uruchamia te testy przed ESP-IDF.

**Fizyczne ESP32-P4 (nie wykonane w tym PR):**

1. Po zimnym starcie Tryb Towarzyski jest wyłączony, brak wskaźnika i spontanicznych komentarzy.
2. Na LCD w ustawieniach włącz tryb; sprawdź opis chmury i licznik 10:00. Porównaj z panelem telefonu.
3. Rozpocznij świadomie zwykłą sesję Xiaozhi i wypowiedz trzy kwestie o kluczach; poza tą sesją brak pasywnego STT. Panel powinien podać licznik kontekstu.
4. Po zakończeniu TTS, gdy Ala jest idle, kliknij **Skomentuj teraz**; posłuchaj jednej wypowiedzi i sprawdź w `/api/reply`. Oceniaj naturalność i adekwatność.
5. Przeprowadź neutralne, poważne i dziecięce scenariusze; odnotuj, że **model może jednak wypowiedzieć coś nietrafnego**, bo obecny backend nie obsługuje cichego wyboru.
6. STOP, upływ 10 min, rozłączenie Wi-Fi, „bez odbioru”, restart: znika wskaźnik, kontekst jest pusty, nie są inicjowane nowe komentarze. **Sprawdź ryzyko spóźnionego TTS**, szczególnie po STOP podczas chmurowego generowania: nie gwarantujemy jeszcze anulowania trwającej odpowiedzi, więc nie reklamować jako stabilne.
7. Przetestuj barge-in, wybudzanie hasłem, TV, mikrofon, FX/głośnik, kamerę, parowanie PIN, ręczne emocje (sticky), 5-sekundowe opóźnienie i X, długie TTS, temperaturę i wolną pamięć przez minimum 30 minut.
8. Testuj tylko pełny flash `espen.bin` z artefaktu PR na właściwej rewizji ESP32-P4; nie publikuj tej kompilacji jako stabilnego obrazu.

## Integracja i regresje

Pierwszeństwo ma bezpieczeństwo/standby, ręczna emocja, aktywna rozmowa, dopiero później reakcje społeczne. `PhonePanel` zachowuje stare URI, PIN, zapis nazwy i emocji. Nowe URI `GET/POST /api/social` są autoryzowane. `Application` pozostaje wyłącznym właścicielem Xiaozhi. Brak możliwości wiarygodnego odróżnienia TV od domowników jednym mikrofonem — tryb nie włącza `auto_speech`.

Niezależny PR #32 (odroczone FX) nie jest zawarty; wymaga oddzielnego testu odsłuchowego i ewentualnego rebasingu przed scaleniem całego stosu.
