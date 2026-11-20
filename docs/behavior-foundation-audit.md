# Ala — audyt podstawy zachowań (2026-11-20)

Zakres: **testy regresyjne i audyt, bez zmiany runtime**. Punkt odniesienia:
`main` / 0.2.17 (`fed6d110a42950a00ab34dc7c2d99e2f5cca8a15`).
Sterowniki sprzętu ESP32-P4, kamera, Wi-Fi, kodek, głośnik i Xiaozhi
pozostają nietknięte. Nowego arbitra emocji **jeszcze nie dodajemy**.

## Mapa właścicieli stanu

| Obszar | Moduły | Aktualny kontrakt |
| --- | --- | --- |
| Twarz | `firmware/face_engine.h`, `firmware/espen_board.cc` | LVGL jest jedynym właścicielem `Tick`/rysowania. `Emotion` (Xiaozhi), `Preview` (telefon), `SocialReact`, `ReplyReact` i gesty współdzielą ten stan. |
| Emocje telefonu | `firmware/phone_panel.h`, `scripts/prepare_phone.py` | PIN + `/api/emotion` → atomowy mailbox → 5 s opóźnienia → `Preview`. Sticky odświeża się co 2,5 s; X resetuje. Identyfikatory: 16 w API, web i Android. |
| Mowa telefonu | `PhonePanel::SendText` / `TrySpeakMood` | 7 s cooldown, 400 bajtów, kontrola zajętości; tylko istniejące `Application::EspenSayText/EspenAskText`. |
| Audio/FX | `emotional_audio.h`, `espen_board.cc` i `scripts/prepare.py` | Emocjonalne cue przez istniejący `AudioService`, limiter, wspólny bufor mowy. Nie dodawać drugiego właściciela PCM. |
| Reakcje społeczne | `social_reactions.h`, `response_reactions.h` | Klasyfikacja STT użytkownika, odpowiadanie mimiką/efektem; semantyka wypowiedzi asystenta na podstawie odtwarzanego TTS. |
| Rozmowa | `conversation_timeout.h`, `scripts/prepare.py` | Stany Listening/Waiting/Speaking, AutoStop, cisza i timeout oczekiwania na odpowiedź. |
| Standby/wybudzanie | `standby_policy.h`, `voice_commands.h`, `espen_wake.h` | „Bez odbioru”/swipe w dół; w standby tylko dotyk lub nauczone offline hasło, nie automatyczny start od zwykłej mowy. |
| TV/barge-in | `speech_onset.h`, `speech_quality.h`, `barge_in.h` | Migracja `tv_guard_v14` i `auto_speech=false`; pierwsze 900 ms TTS chronione, wymagany potwierdzony foreground. Jeden mikrofon **nie identyfikuje** wiarygodnie TV. |
| Firmware | `scripts/build.sh`, `web/manifest-full.json`, `web/index.html` | Główny instalator: wyłącznie pełne `espen.bin` @ `0x0`. Manifest, build i pilot mają zgodną wersję. Firmware publikować dopiero po kompilacji i sprawdzeniu układu/SHA256. |

## Zidentyfikowane konflikty

**Wysoki — priorytety mimiki.** Sticky telefonu co 2,5 s wywołuje
`FaceEngine::Preview`, które czyści `social_`. Wypiera spontaniczne reakcje
społeczne, a czasem emocje Xiaozhi. Nawet `Preview(Neutral)` po X maskuje
nowe emocje chmury przez 4,2 s. Nowy arbiter musi scentralizować priorytety
i TTL; nie naprawiać tego kolejnym niezależnym timerem.

**Wysoki — gubienie wypowiedzi.** `prepare_phone.py` po jednym wywołaniu
`TrySpeakMood` ustawia `phone_voice = false` nawet, gdy robot jest zajęty
albo trwa cooldown 7 s. Animacja zostaje przyjęta, ale dopasowana fraza
może przepaść. Rozwiązać później przez limitowany pending+ack, nie przez
ominięcie Xiaozhi. Przyszłe testy muszą objąć przerwanie i anulowanie.

**Wysoki — FX/TTS.** W `main` 0.2.17 efekt dźwiękowy może nie wejść do kolejki
w trakcie TTS. PR **#32** (0.2.18) proponuje odroczenie FX do końca
odtwarzania, poprawę limitera i wzmocnienie efektów. Nie reimplementować
tego w równoległej gałęzi; potrzebny niezależny odsłuch fizycznej płytki.

**Wysoki — TV/autonomia.** VAD i STT nie rozróżniają niezawodnie głosu
domownika od telewizora przy jednym mikrofonie. Autonomiczne wtrącanie
wymaga zgody/polityki ciszy, limitu komentarzy i pewności adresowania;
nie wolno włączać auto-speech domyślnie.

**Średni — równoległe piloty.** Na `main` działa `android/app` oraz lokalny
panel web. PR **#22** i **#23** zawierają inne aplikacje Android; PR **#25**
próbuje dołożyć inny backend/panel z 0.2.15 (konfliktujący z aktualnym).
Nie scalać ich mechanicznie ani nie zastępować aktywnego pilota.

**Średni — trwałość emocji.** PR **#20** proponuje zapis nastroju w NVS,
PR **#21** progresję serduszek o własnych timeoutach. Konfliktują z tymi
samymi polami `FaceEngine`/`response_reactions`. Trzeba oddzielić
trwały wybór użytkownika od krótkiej reakcji społecznej.

**Średni — wersjonowanie.** PR **#32** zmienia `build.sh`, `prepare.py`,
`espen_board.cc` i manifesty na 0.2.18. Wymagany rebase i zielony build
na faktycznym merge commit przed wdrożeniem. PR **#33** to tylko roadmapa,
bez konfliktu runtime.

## Bramka akceptacji

1. `tests/test_behavior_contracts.py`: zgodność 16 emocji, API/PIN,
   opóźnień, standby/TV, Xiaozhi oraz obu instalatorów/wersji.
2. `tests/test_behavior_foundation.cc`: rzeczywiste hostowe silniki twarzy,
   standby, komendy „bez odbioru”, timeouty rozmowy i TV bez referencji TTS.
3. `.github/workflows/behavior-guards.yml`: szybki niezależny check na PR.
4. `scripts/build.sh`: powyższe testy przed pełną kompilacją ESP-IDF;
   istniejący workflow weryfikuje pełny flash, sumy SHA i publikuje Pages
   **tylko dla push na main**, nie dla Pull Request.

**Ograniczenie:** hostowe i statyczne testy nie dowodzą działania sprzętu.
Po zielonym CI: sprawdzić 16 emocji z telefonu/web, 5 s, X/sticky, TTS,
odgłosy, touch/keyword w standby, telewizor w tle, barge-in, poprawny
start po pełnej instalacji i zachowanie ustawień z kopii zapasowej.

## Następny silnik

Nowy arbiter powinien odbierać ograniczone zdarzenia (`source`, `priority`,
`request_id`, `expires_at`, `ack`) i decydować o zachowaniu, lecz **nie
przejmować** wątku LVGL, kodeka, mikrofonu czy klienta Xiaozhi. Priorytety
do zatwierdzenia: bezpieczeństwo/standby i ręczne przerwanie, jawne polecenie
użytkownika, aktywna rozmowa, reakcje społeczne, spontaniczny gest.
Domyślnie milczeć przy niepewnym źródle wypowiedzi.
