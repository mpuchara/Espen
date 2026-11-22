# Ala — centralny silnik zachowania (etap 2)

Bazą gałęzi jest `main` / `fed6d110` (firmware 0.2.17).
Włączono selektywnie z niescalonego **PR #34** testy hostowe,
kontrakty, szybki CI oraz audyt (bez automatycznego merge).
**PR #32** (0.2.18, odroczone FX i głośność) pozostaje osobną zmianą
i **nie** jest tu cherry-pickowany: po jego ewentualnym scaleniu wymaga
odrębnego rebase oraz odsłuchu. **PR #33** jest wyłącznie roadmapą.
Alternatywne implementacje panelu / stanu emocji z PR #20/#21/#25
nie zostały automatycznie scalone. Numeru wersji i instalatora nie zmieniono.

## Właścicielstwo

- `firmware/ala_behavior_engine.h` — czysta lokalna polityka C++17:
  przyjmuje zdarzenia, przechowuje ulotne terminy, zwraca `BehaviorDecision`.
  Używa istniejącego `espen::Mood` (16 identyfikatorów, bez drugiej listy).
- `espen_board.cc` — jedyny wątek właścicielski LVGL: przekazuje eventy
  Xiaozhi (mailbox atomowy), rzeczywistego TTS, transkrypcji/STT oraz dotyku;
  odbiera decyzje i wywołuje `FaceEngine::ManagedMood`.
- `FaceEngine` — **jedyny** renderer, animacje intro, loopy, serca, mruganie,
  patrzenie, ruch ust do prawdziwego audio. Po zainstalowaniu arbitra
  wcześniejsze losowe micro-emocje i startle od hałasu nie wybierają mimiki.
  W trybie ręcznej emocji istniejące animacje odpowiedzi nie mają prawa
  przejąć wizualnego priorytetu.
- `PhonePanel` — 8-cyfrowy PIN, HTTP bez chmury, spójny snapshot chroniony
  krótkim mutexem. `prepare_phone.py` dostarcza wyłącznie komendy LVGL.
  Odliczanie 5 sekund jest **wyłącznie w behavior engine**. Nie ma
  cyklicznego `Preview`, który wcześniej kasował `social_`.
- Wydawanie dźwięków, TTS, kodek, kamera, Wi-Fi, Xiaozhi, NVS, wake phrase,
  TV guard, przerwania TTS i zasady standby — bez nowego toru. Robot **nie**
  generuje samodzielnych wypowiedzi ani aktywnego nasłuchiwania społecznego.

## Priorytety

| Pierwszeństwo | Źródło | Czas |
| ---: | --- | --- |
| 100 | Standby / bez odbioru | do jawnego wybudzenia |
| 90 | Telefon / ręczny wybór | stale do X lub 4,2 s po 5 s opóźnienia |
| 80 | Dotknięcie / gest | 1,7 s |
| 70 | Xiaozhi / faktycznie odtwarzana odpowiedź | okres mowy / krótki TTL |
| 60 | STT użytkownika / społeczna reakcja | 0,65–2,4 s |
| 40 | Nastrój po emocji / po TTS | do 14 s po końcu TTS |
| 20 | Spokojna/senna bezczynność | po 2 min |
| 10 | Lokalne mikro-animacje | 1,4 s co ~27 s, gdy ekran nie śpi |

Pochwała: `Surprise` → `Happy`; żart/łaskotki:
`Wink` → `Laugh`; pytanie trudne: `Thinking` → afterglow;
bezczynność: neutralna → senna. Zmiany niskich priorytetów mają
900 ms minimalnego czasu przebywania. Szczegóły pozy animuje istniejący
`FaceEngine`, bez alokacji w cyklu rysowania.

**X** unieważnia aktywny lock oraz jeszcze niewykonany timer; każda kolejna
komenda ma numer sekwencji. Tymczasowa i trwała emocja mają to samo
pierwszeństwo. `TrySpeakMood` zachowuje oryginalny kanał i cooldown;
w przypadku zajętości ma ograniczone retry co 400 ms, maksymalnie do
10 sekund od wyświetlenia (albo do anulowania/końca podglądu).

Stan nie jest zapisywany do NVS. Zegar używa różnic `uint32_t` —
porównania obowiązują dla okien krótszych niż 2^31 ms.

## Decyzje społeczne i cisza

Obecne API `BehaviorDecision` przewiduje ocenę animacji i spontanicznej
mowy. `AllowSpontaneousSpeech()` oraz `allow_spontaneous_speech`
zwracają bezwarunkowo `false`. Brak interpretacji VAD jako adresowania
do robota; TV nie może uruchomić autonomicznej konwersacji. Reguła
keyword-only w `standby_policy.h` i domyślne `auto_speech=false`
pozostają nienaruszone.

Log wypisywany wyłącznie przy zmianie źródła/mimiki: wybrany `Mood`,
źródło, priorytet, TTL, blokada telefonu i liczniki odrzuconych mikroreakcji.
Nie logujemy klatek ani wyrazów użytkownika.

## Walidacja / ograniczenia

1. `tests/test_behavior_foundation.cc` i
   `tests/test_behavior_contracts.py` przeniesione z PR #34.
2. `tests/test_ala_behavior_engine.cc`: priorytety, 5 s, sticky,
   X/stale request, wygaszenie, pochwały, standby/TV, brak TTS interrupt,
   burza eventów, offline, restart, przepełnienie licznika.
3. `tests/test_phone_panel.py`, `tests/test_mood_voice.py`:
   kontrakty nowego bridge, bez naruszenia HTTPS/PIN/uprawnień.
4. `.github/workflows/behavior-guards.yml` oraz `scripts/build.sh`
   uruchamiają testy hostowe. Główny CI buduje z pinem ESP-IDF, weryfikuje
   pełne `espen.bin` i SHA. Publikacja Pages na PR wyłączona.

**Bramka sprzętowa (NIE sprawdzono na fizycznej Ali):**
pełny boot ESP32-P4; emocje z Androida i WWW (5 s, X natychmiast,
sticky >1 min, zwykła 4,2 s); poprawna mowa telefonu z cooldownem
i przerwaniem; serca oraz mimika podczas długiego TTS; komplement / żart
w realnym STT; swipe „bez odbioru”, dotyk/hasło, TV w tle;
płynność LVGL, obciążenie CPU, zużycie RAM i zgodność odgłosów
po ewentualnym PR #32. Bez tych testów nie publikować firmware jako
zweryfikowanego sprzętowo.
