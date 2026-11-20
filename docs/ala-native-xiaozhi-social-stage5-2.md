# Ala — Etap 5.2: Native Xiaozhi Social Autonomy — audyt i bezpieczny zakres

**Baza:** PR #38 (`feature/ala-social-intelligence-stage5`). Nie bazuje na PR #39 ani #40. Firmware 0.2.17; brak publikacji stabilnego instalatora. Jedyną usługą AI pozostaje istniejące Xiaozhi; nie trzeba instalować dodatkowego serwera ani podawać nowych kluczy.

## Najważniejsze ustalenie

**Na obecnym publicznym protokole Xiaozhi nie potwierdzono działającego `STT_ONLY → semantic_verdict → conditional_TTS`.** Serwer wysyła tekst STT do klienta i steruje syntezą mowy, ale brak udokumentowanego sposobu atomowego przekazania decyzji lokalnemu arbitrowi przed generowaniem TTS. Nie wolno reklamować automatycznych ripost ani pasywnego podsłuchu jako gotowych.

Dlatego w tym PR Tryb Towarzyski pozostaje **świadomie włączany na 10 minut**, kontekst ograniczony do 5 transkrypcji z normalnej sesji Xiaozhi, a **„Skomentuj teraz”** jest nadal ręczną, prawdziwą prośbą do agenta Xiaozhi o komentarz. Lokalna bramka spontanicznego TTS jest jawnie zamknięta. Nie uruchamia się dodatkowy proces, serwis, LLM ani audio capture.

## Analiza potwierdzonego protokołu

Źródło: [oficjalna dokumentacja WebSocket xiaozhi-esp32](https://github.com/78/xiaozhi-esp32/blob/main/docs/websocket.md), przypięty upstream `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` z `scripts/build.sh`, `main/application.cc`, `main/protocols/protocol.cc`.

| Punkt | Potwierdzone | Brak potwierdzenia |
| --- | --- | --- |
| PCM/Opus z mikrofonu | Istniejący AudioService koduje wejście, Opus trafia do transportu | Niezależny, ciągły upload STT_ONLY |
| Start/koniec nasłuchu | `listen.start/stop/detect`, auto/manual/realtime | Tryb cichej analizy kolejnych tur domowników |
| STT | `type=stt, text` w aktywnej sesji, callback UI | Semantyczny werdykt modelu przed syntezą |
| LLM | `type=llm, emotion` zmienia mimikę; model na serwerze generuje tekst | Dedykowany `IGNORE/COMMENT/...` z identyfikatorem tury |
| TTS | `tts.start/sentence_start/stop` i pakiety binarne Opus | `speak_if_approved`, `skip_tts`, identyfikator tury |
| Anulowanie | Klient może wysłać `abort` | Gwarancja odwołania wszystkich pakietów konkretnej tury |
| MCP | `mcp.tools/call` steruje narzędziami klienta | Potwierdzony, bezgłośny werdykt niezależny od TTS |
| Instrukcja agenta | Można dostosować osobowość, ale to nie osobne API | Pewne wyłączenie syntezy w wyniku samej instrukcji |
| `tts.sentence_start.text` | Tekst napisu bieżącego zdania | To **nie** jest bezpieczny, wcześniejszy kanał decyzyjny |
| Agent/HTTP „Skomentuj” | Rozszerzenie Espen `EspenAskText → SendWakeWordDetected` korzysta z bieżącego Xiaozhi | `SendWakeWordDetected` nie stanowi oficjalnego API `STT_ONLY` |

Samo odrzucenie dźwięku na ESP32 po przyjściu `sentence_start` **nie blokuje syntezy na serwerze** i nie gwarantuje uniknięcia pierwszych ramek audio, opóźnionych odpowiedzi czy przerwania prawidłowej rozmowy. Nie implementujemy fikcyjnego protokołu.

## Wdrożone w Etapie 5.2

- `scripts/prepare_phone.py` odczytuje **rzeczywisty stan otwartego kanału audio Xiaozhi** z callbacków `OnAudioChannelOpened/Closed`, resetu i rebootu. Nie zmienia protokołu ani toru audio.
- `firmware/ala_social_intelligence.h`: stała `kVerifiedNativeXiaozhiAutonomy = false` oraz licznik `manual_requests`. Sam `Evaluate()` nadal zachowuje się zgodnie z PR #38, ale `PhonePanel::SocialSpeechPermission()` nie odblokuje spontanicznego TTS przy niesprawdzonym serwerze.
- `GET /api/social` jawnie pokazuje `native_autonomy:false`, `conditional_tts:false`, `xiaozhi_channel_open`, `manual_requests`, `comments`, `skipped` i `cancelled`; poprzednie pola są zachowane. `manual_requests` zlicza zaakceptowane **żądania**, a nie potwierdzone wypowiedzi.
- Panel telefonu prezentuje stan sesji Xiaozhi, rozdziela prośby ręczne od autonomicznych i nigdy nie wyświetla „Autonomia działa”. Zamknięty kanał przy idle nie oznacza automatycznie awarii Xiaozhi.
- Zachowany tryb „bez odbioru”, STOP, wake word, barge-in, ochrona TV, emocje sticky/X, Living Robot Engine, kamera, Wi-Fi, PIN i zwykła rozmowa przez Xiaozhi. Żadnych sterowników sprzętowych, nowego serwera czy klucza API.

**Ograniczenie STOP:** lokalna zgoda i kontekst są zerowane; opóźniona/już rozpoczęta odpowiedź chmurowa z wcześniejszego ręcznego polecenia może jednak dokończyć odtwarzanie. Nie można uznać STOP-during-TTS za spełniony bez osobnej, wiarygodnej obsługi na granicy serwer/klient. Nie wolno tego ukrywać.

## Uruchamianie testów

`tests/test_ala_social_intelligence.cc` obejmuje zgody, generację, wersję kontekstu, limity, ręczne prośby i statycznie zablokowaną autonomię. `tests/test_ala_social_contracts.py` i `tests/test_ala_native_xiaozhi.py` kontrolują integrację oraz brak dodatkowego backendu; to **testy hostowe i statyczne, nie symulacja publicznego serwera Xiaozhi**. Workflow host/build dopisano na bazę PR #38; pełny ESP-IDF build ma weryfikować `espen.bin` ≤ 16 MiB i SHA256. Uruchomienie na fizycznej Ali pozostaje konieczne.

## Testy na prawdziwym robocie (nie wykonane)

1. Zimny start: Tryb Towarzyski OFF, brak transmisji pasywnej i komentarzy.
2. Standardowa rozmowa: wake word, STT, TTS, mimika i barge-in bez zmian; status kanału otwiera się po handshake.
3. Aktywacja z LCD i telefonu: max 10:00, jawna informacja o mikrofonie/chmurze, STOP/timeout/reboot zerują kontekst.
4. Wypowiedz kwestie o kluczach w **normalnej sesji**; sprawdź 5 ostatnich fragmentów i ręczny „Skomentuj teraz”; licznik ręcznych żądań rośnie po zaakceptowaniu.
5. Sytuacja poważna, cisza, telewizor, wiele osób: żadnych samodzielnych odpowiedzi; w trybie ręcznym model może się pomylić.
6. STOP, „bez odbioru”, utrata Wi-Fi/Xiaozhi, późne STT, błędna odpowiedź, ponowna aktywacja, 32-bitowy rollover.
7. STOP podczas oczekiwania i TTS: **nie zaliczać jako pełnej gwarancji anulowania**.
8. Test emocji z telefonu (sticky, X, 5-sekundowe opóźnienie), odgłosów, kamery, ekranu i panelu.
9. Przynajmniej 30 minut obciążenia; temperatura, pamięć, brak restartów.
10. Wyłącznie **pełny obraz** `espen.bin` z CI właściwego PR, sprawdzony SHA256; nie publikować jako stabilny.

## Co byłoby niezbędne do rzeczywistej autonomii

Publiczny Xiaozhi musiałby **potwierdzić** ciche przetwarzanie STT, ustrukturyzowany werdykt semantyczny przed TTS, identyfikator tury i rewizji kontekstu, syntezę dopiero po lokalnym pozwoleniu, `IGNORE` bez generowania audio oraz anulowanie zaległych pakietów bez zmiany zwykłego trybu. Do tego czasu wszystkie automatyczne riposty pozostają wyłączone — **bez dodatkowej infrastruktury**.
