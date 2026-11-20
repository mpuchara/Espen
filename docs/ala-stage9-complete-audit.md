# Ala Stage 9 — Complete System Audit & Stabilization (raport wstępny)

**Data audytu:** 2026-11-20 · **Repo:** [mpuchara/Espen](https://github.com/mpuchara/Espen) · **Status:** **NO-GO / eksperymentalna**.

## 1. Cel, źródło prawdy i ograniczenia

Ala jest rodzinnym robotem dla dzieci ok. 6–12 lat, a nie kontrolerem inteligentnego domu. Docelowy zakres: oryginalne **Xiaozhi**, ESP32-P4, OV02C10, lokalne emocje/zachowanie/pamięć, opcjonalny panel telefonu. **Żadnego Home Assistant, dodatkowego serwera AI ani Social Autonomy Bridge**.

Punkt odniesienia to PR [#45](https://github.com/mpuchara/Espen/pull/45) HEAD `9b24d14c99dd0aa26dc8b9aca325b01702bcedcb`, oparty na PR #43 i scalający dorobek PR #44. Gałąź audytu: `audit/ala-stage9-stabilization`. Bazowy `main` w momencie przeglądu: `fed6d110a42950a00ab34dc7c2d99e2f5cca8a15`. Nie modyfikowano `main`, nie zamykano PR, nie publikowano firmware jako stable.

**Rozróżnienie dowodów:** „Zaimplementowane” oznacza obecność odpowiednich ścieżek w źródłach; „CI PASS” oznacza wyniki automatyczne dla konkretnego SHA; „NOT TESTED” oznacza brak weryfikacji fizycznej. Połączenie tych dwóch pierwszych NIE jest dowodem działania robota w domu.

## 2. Ocena funkcjonalności

| Obszar | Potwierdzone w źródłach PR #45 | Ograniczenie / do sprawdzenia |
|---|---|---|
| Rozmowa po polsku | Połączenie z istniejącym Xiaozhi, STT/TTS i obsługa cyklu rozmowy | Dokładność słyszenia i bezpieczeństwo odpowiedzi AI niezmierzone na Ali |
| Silnik zachowania | `AlaBehaviorEngine` + Living Robot Engine + Conversation Intelligence; priorytety ręcznych emocji, stanów, autonomii | Kolejność zdarzeń przy dotyku, telefonie, Xiaozhi i restarcie — test fizyczny |
| Mimika | Mruganie, dotyk, gesty, nastrój, wyraz twarzy; emocje z telefonu | Naturalność i czytelność; testy ręcznej emocji po 5 s oraz trwałości do X |
| Odgłosy | Syntezator lokalnych efektów i cue wywoływane z interakcji | **Zidentyfikowana regresja:** brak zmian z PR #32 w #45; naprawa kandydacka w Stage 9 |
| Słuchanie | Jeden tor AudioService, istniejący AFE/VAD/Opus, 300-ms pre-roll / obsługa replay | STT „Siema/Hej/Cześć/Tak/Nie” i TV nieprzetestowane fizycznie |
| Rozpoznanie twarzy | Detektor `MSRMNP_S8_V1`, `MFN_S8_V1`, lease kamery, opt-in, 4 próbki, maks. 5 profili RAM | W praktyce eksperymentalne; brak testów FAR/FRR, spoof i multi-face |
| Rozpoznanie mówcy | Matcher wektorów i logika stanów tożsamości | **NIE DZIAŁA:** brak providera embeddingów, runtime i kalibracji; brak realnego łączenia face+voice |
| Pamięć | `AlaMemoryEngine`, 20 wpisów do 160 B, opt-in, NVS, import/eksport, usuwanie i panel PIN | Xiaozhi korzysta z pamięci tylko przez jawne przekazanie danych; brak automatycznego recall w każdej rozmowie |
| Tryb Towarzyski | Lokalna polityka i manualne komentarze przez istniejące Xiaozhi | `kVerifiedNativeXiaozhiAutonomy = false`: nie obiecywać samodzielnych ripost po odsłuchiwaniu |
| Telefon | Lokalny HTTP :80, 15 tras, PIN, emocje, tekst→mowa, social, pamięć, face, hearing | HTTP nie szyfruje PIN ani danych; brak potwierdzonego limitu prób PIN |
| Pełna instalacja | Manifest pełnego `espen.bin` od offsetu 0 | Test na realnej płycie i backup pamięci nadal konieczny |

**Gry/zagadki/opowieści:** Xiaozhi może realizować je konwersacyjnie; nie są one osobnymi lokalnymi grami ani zaimplementowanym silnikiem zadań.

## 3. Wyniki audytu kodu i ryzyka

### A9-01 — P1, potwierdzona regresja odgłosów (naprawa w gałęzi audytu)

PR [#32](https://github.com/mpuchara/Espen/pull/32) miał na HEAD zielony build, ale jego istotne zmiany **nie weszły do PR #45**. Porównano faktyczne pliki, nie samą nazwę PR. W #45: `QueueEspenEmotionalCue(int cue,int gain)` odrzucał dźwięk, gdy AudioService był zajęty; `espen_fx_response_attempted` było zużywane mimo braku enqueue; domyślny FX gain wynosił 45%; limit amplitudy PCM wynosił 7600; brakowało dźwięków głaskania oraz reakcji społecznych z PR #32. Powodowało to możliwość praktycznej ciszy odgłosów w normalnej rozmowie mimo istniejących plików.

**Kandydacka naprawa:** przeniesiono wybrane, wcześniej zbudowane zmiany z PR #32 do aktualnej architektury #45, z zachowaniem `ReactUserGesture` oraz `behavior_.SocialReaction` i z zachowaniem wersji **0.2.17** (brak release):
- Odroczony pojedynczy efekt po *rzeczywistym* zakończeniu odtwarzania TTS; czyszczenie pending przy reset/cancel.
- Brak kolejkowania przejściowych UI cue na później; bez nakładania PCM na mowę.
- Wzrost maks. amplitudy generowanych PCM do 15000 (zachowany zapas cyfrowy) i **domyślnie** gain 70; zapisane ustawienia użytkownika nadal mają pierwszeństwo.
- Sound cues dla częstych odpowiedzi, ograniczenie częstotliwości oraz wybranych interakcji (głaskanie, łaskotanie, nos, reakcje słowne).
- C++ regresje oraz guard `tests/test_audio_cue_integration.py` dodany do `scripts/build.sh`.

**NIE POTWIERDZONO** słyszalności na głośniku: build/źródła nie zastępują testu PA, kodeka, głośności i przerwania TTS. Nie podnoszono na ślepo głośności kodeka.

### A9-02 — P1, bezpieczeństwo panelu i prywatność

`PhonePanel::Authorized` sprawdza ośmiocyfrowy PIN; API pamięci/twarzy/statusu żąda autoryzacji. Serwer to **niezaszyfrowany HTTP na porcie 80**. W tym przeglądzie nie stwierdzono limitera prób błędnego PIN w `Authorized`; nie należy uznawać samego PIN za zabezpieczenie przed podsłuchem sieci i brute force. **Nie stwierdzono bypassu w przeglądanych trasach**, ale brak testu penetracyjnego. Nie otwierać portu do Internetu, używać wyłącznie zaufanej sieci, rozważyć bezpieczny, nieblokujący limiter w oddzielnej zmianie z testami równoległych żądań, restartów i legalnych prób.

Profil twarzy to **biometria w RAM** tylko na opt-in; zniknie po restarcie. Pamięć rodzinna w NVS jest z natury wrażliwsza — wymaga zgody dorosłego, ograniczenia przechowywanych danych i ręcznego czyszczenia. Brak nagrywania całych rozmów. NVS i eksport nie zapewniają szyfrowania. Face recognition nie służy do logowania.

### A9-03 — P1, brak miarodajnego testu słuchania / pre-roll

PR #44/#45 zawiera `AlaSpeechPreroll::ReplayStep` podający starsze ramki przed żywymi i testy hostowe dla 100–400 ms. Nie przedstawiono pomiaru *realnego* VAD, Opus, STT Xiaozhi, opóźnień ani TV. Przed deklaracją poprawy trzeba oddzielić: utratę początku PCM, ucięty koniec, nieprawidłowy transcript, niewłaściwy barge-in i fałszywą aktywację z telewizora. **Nie zmieniać progów w ciemno.**

### A9-04 — P1, twarz niezweryfikowana na żywych osobach

Kod korzysta z rzeczywistego `MFN_S8_V1`, ale progi 0.65 cosine / 0.10 margin / 3 potwierdzenia są eksperymentalne. Niezbędny pomiar nieznanej osoby, zdjęcia (replay), podobnych twarzy, wielu twarzy, oświetlenia, temperatury/PSRAM i obciążenia Xiaozhi. **Nie wolno** mylić obecności modelu z potwierdzoną identyfikacją dziecka.

### A9-05 — P2, dokument historyczny z nieaktualną tabelą partycji

`docs/ala-stage7a-7b-integration.md` zawiera starsze ograniczenie `0x3f0000`; dla obecnej gałęzi obowiązuje `firmware/partitions_face_16m.csv`, aplikacja `0x7e0000`, assets `0x800000`. Nie kopiować starej instrukcji do aktualizacji. Wszystkie aktualizacje użytkownika są **pełne**.

### A9-06 — P2, ograniczenia autonomii i harmonogramowania

Istnieje wiele inicjatorów emocji/animacji (Xiaozhi, Behavior, Living, Social, Phone), jednak priorytet trwałej emocji z telefonu jest obsługiwany przez istniejący arbitraż. Testy hostowe sprawdzają kontrakty, ale nie dowodzą braku konfliktu na jednoczesnej pracy LVGL/AudioService. Wyłącznie ręczne, bezpieczne poprawki konfliktów potwierdzonych pomiarem. **Nie dodawać kolejnego silnika.**

### A9-07 — P2, ryzyka doświadczenia dziecka

Trzeba zweryfikować: żart i przyjazny ton, brak zaborczych („tylko ja cię rozumiem”) lub tajnych („nie mów rodzicom”) komunikatów, brak przedstawiania siebie jako człowieka, brak nieodpowiednich poleceń sprzętowych, wizualna sygnalizacja mikrofonu/kamery i wyłączanie „bez odbioru”. Klasyfikator emocji nie jest moderacją treści wygenerowanej przez Xiaozhi. Testy muszą obejmować faktyczne odpowiedzi usługi AI, nie tylko firmware.

## 4. Przegląd PR (stan 2026-11-20)

`PASS` poniżej oznacza stan odczytanych runów GitHub Actions *na HEAD danego PR*, nie wynik audytu fizycznego.

| PR | Zakres | Baza | Stan / CI | Kod w #45? | Działanie |
|---|---|---|---|---|---|
| #32 | Emotional Audio 0.2.18 | main | open, firmware PASS | **NIE** (kluczowe poprawki) | Wybrane poprawki przeniesione do Stage 9; nie scalać całego release |
| #34 | Behavior Foundation | main | open, firmware+contracts PASS | Testy/kontrakty obecne | pozostawić do uporządkowania |
| #35 | Central Behavior | main | open, oba PASS | TAK | traktować jako część łańcucha |
| #36 | Living Robot | #35 | open, oba PASS | TAK | łańcuch |
| #37 | Conversation Intelligence | #36 | draft, oba PASS | TAK | łańcuch |
| #38 | Social Intelligence | #37 | draft, oba PASS | TAK | łańcuch |
| #39 | Social Bridge z backendem | #38 | draft, oba PASS | NIE | **WYKLUCZYĆ**, kandydat do zamknięcia |
| #40 | Stabilizacja zewnętrznego Bridge | #39 | draft, oba PASS | NIE | **WYKLUCZYĆ**, kandydat do zamknięcia |
| #41 | Native Xiaozhi Social | #38 | draft, oba PASS | TAK | łańcuch, fail-closed |
| #42 | Personal Memory | #41 | draft, oba PASS | TAK | łańcuch, opt-in |
| #43 | Face Recognition | #42 | draft, oba PASS | TAK | baza #45 |
| #44 | Advanced Hearing | #42 | draft, oba PASS | TAK, zintegrowano w #45 | nie scalać niezależnie |
| #45 | Multimodal Integration | #43 | draft, oba PASS | BAZA | bez merge do main przed testami |
| #46 | Home Assistant | #45 | draft, contracts PASS, firmware **FAIL** | NIE | **WYKLUCZYĆ**, kandydat do zamknięcia |

Zalecane porządkowanie: po akceptacji Stage 9 i fizycznym teście wybrać **jeden** zweryfikowany łańcuch integracji, udokumentować zależności, następnie *ręcznie* zdecydować o mergach/retargetowaniu/zamknięciach. Nie scalać wszystkich PR pojedynczo na `main` i nie rozszerzać produktu o bridge/HA.

## 5. Weryfikacja kompilacji i flash (twardy punkt odniesienia)

Sprawdzono istniejący zielony run PR #45: [firmware #44807686323](https://github.com/mpuchara/Espen/actions/runs/44807686323) i [contracts #44807686556](https://github.com/mpuchara/Espen/actions/runs/44807686556). Build jest dla **oryginalnego HEAD #45**, nie dla zmian Stage 9.

- `xiaozhi.bin`: **0x7486a0 = 7 636 640 B**.
- Rozmiar partycji aplikacji: **0x7e0000 = 8 257 536 B**.
- Rezerwa: **0x97960 = 620 896 B (~7.5%)**.
- Pełny obraz: **0x9d33fe = 10 302 462 B**, w limicie flash 16 MiB.
- Bootloader obecny na `0x2000`, app `0x20000`, assets `0x800000`; GitHub Actions potwierdził kontrolę sum SHA256.
- Nie znamy jeszcze rozmiaru ani sumy SHA256 **obrazu z poprawkami Stage 9**. Należy odczytać je z runu tego PR, nie przepisywać starych wartości.

Oryginalny PR #32 przeszedł [build #44718959860](https://github.com/mpuchara/Espen/actions/runs/44718959860), jednak zintegrowana gałąź wymaga osobnego pełnego buildu. Ryzyko flash headroom i RAM/PSRAM wyklucza dodawanie nowych modeli.

## 6. Testy, luki w dowodach, regresje

**Wykonane wcześniej:** firmware + contracts CI dla #45: PASS; dla #32: firmware PASS. **Dodane w Stage 9:** test wzajemnych kontraktów `tests/test_stage9_regression_contracts.py` (wykluczenie HA/bridge, prawdziwy status voice), test `tests/test_audio_cue_integration.py`, rozszerzone testy C++ syntezatora i klasyfikatora odpowiedzi; wszystkie w ścieżce `scripts/build.sh`.

**Nie wykonane przez audyt na fizycznym robocie:** bootloop, rzeczywista słyszalność, rozpoznawanie krótkich słów, rozpoznanie osób, 60-minutowa stabilność, temp i PSRAM, wybudzenie z „bez odbioru”, usterki Wi-Fi, realna jakość odpowiedzi Xiaozhi. **Status: NOT TESTED**, nie „PASS”.

### Checklista akceptacji na prawdziwej Ali — do wykonania

| Seria | Scenariusz i liczba prób | Rejestrowane wyniki |
|---|---|---|
| A. Start | 5 zimnych startów; 5 restartów; 3 pełne instalacje **`espen.bin`** z kopią pamięci | boot/no bootloop, Wi-Fi, Xiaozhi, ekran, ustawienia |
| B. 30 dialogów | 10 powitań/proste pytania; 10 żartów, opowieści, zagadek; 10 nauka, poprawianie zdań, emocje | sensowność/bezpieczeństwo wypowiedzi, responsywność i reakcje |
| C. Krótkie słowa | 10× każde: **Siema, Hej, Cześć, Tak, Nie**; najpierw cisza, potem TV w tle | osobno *onset PCM*, *transcript Xiaozhi*, czas, false wake |
| D. Dźwięki | Po 5 różnych emocji, 5 reakcji dotykowych, 10 odpowiedzi TTS z efektem, 10 barge-in/STOP | FX/TTS/mute, brak nakładania, brak spóźnionego efektu po STOP |
| E. Emocje | 10× emocja z telefonu po 5 s, utrzymanie, X, równoczesny dialog i dotyk | priorytet, poprawność stanu, animacja, PCM lip-sync |
| F. Twarz | 2 profile × 4 próbki, 10 prób znajomy/nieznany, 5 scen multi-face, światło i zdjęcie w telefonie | false accept/reject, UNKNOWN, RAM reset, kamera, consent |
| G. Pamięć | ON/OFF, dodaj/edytuj/usuń/import/eksport, restart, błędny PIN, pełny flash | trwałość, zgoda, limit, brak przecieku, jawne Xiaozhi |
| H. Telefon | błędny i poprawny PIN, rozłączenie, równoległe żądania, duże body | 401/400, brak wycieków, brak zawieszania |
| I. 60 minut | naprzemiennie Xiaozhi, animacje, kamera, dotyk, pamięć, telefon, TV, Wi-Fi loss | **0 restartów/hangów**, heap/PSRAM min i największy blok, stosy, lagi LVGL, audio queue |

W każdym przypadku zapisać datę, commit, wersję obrazu, metryki i surowe wyniki pass/fail. Przed/po porównywać #45 z Stage 9 przy identycznej głośności, odległości, TV i konfiguracji. **Nie twierdzić „lepsze STT” bez policzonych prób i warunków**.

## 7. Wymagane działania po Stage 9 i GO/NO-GO

1. Zweryfikować nową kompilację ESP32-P4 z audytowego HEAD, dokładne `espen.bin`, SHA256, partycje, brak nowego bootloopu; w razie fail poprawiać tylko udowodnione konflikty.
2. Na fizycznej Ali wykonać checklistę z pkt 6; przetestować 60 min, telewizor, 30 dialogów, dotyk i barge-in.
3. Potwierdzić bezpieczeństwo dzieci i panelu w zaufanej sieci; zaprojektować osobno limiter PIN po ocenie ryzyka DoS, a dane rodzinne trzymać minimalnie.
4. Zebrać wyniki jakości twarzy i słów; nie włączać modelu rozpoznawania mówcy i nie zmieniać layoutu flash.
5. Dopiero po eliminacji P0/P1 przygotować **jedną** wersję stabilną do **pełnej instalacji**. Bez automatycznego merge, bez HA, bez nowego serwera AI.

**Rekomendacja na 2026-11-20: NO-GO dla wersji stabilnej.** Istnieje mocna baza kodowa i zielony dotychczasowy build, ale potwierdzono regresję odgłosów w PR #45 i nie istnieje dowód testu fizycznego całego stosu. Stage 9 dostarcza kandydacką naprawę, raport i guardy, nie certyfikat stabilności.
