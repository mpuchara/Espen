# Ala — Etap 4: Conversation Intelligence (stacked PR na #36)

## Baza / zakres

Etap 4 bazuje na niepołączonym PR #36 (Living Robot Engine), który bazuje
na #35 (Behavior Engine). PR #34 to testowa podstawa przeniesiona do #35;
PR #32 (odroczone odgłosy i gain) **pozostaje niezależny**.
Nie wolno scalać tych PR automatycznie ani kopiować starych modyfikacji audio.

ESP32-P4 / Xiaozhi pozostaje **jedynym właścicielem** mikrofonu, strumienia
I2S, sesji sieciowej i przejść `Application::SetDeviceState`.
`AlaConversationManager` jest lekką polityką czasu, a **nie** niezależną
maszyną sterującą Xiaozhi. To samo co wcześniej wejście VADNet1 w istniejącej
kolejce zdarzeń; brak nowych callbacków realtime, tasków, buforów, NVS,
nagrywania, zmiany protokołu, GPIO, kamery i obsługi Wi-Fi.

## Wykrywanie końca wypowiedzi

Wcześniej: endpoint AutoStop po **2000 ms** ciszy, brak odróżnienia krótkich
od długich wypowiedzi. Nowa polityka (w `firmware/ala_conversation_manager.h`):

| Zaobserwowane przez VAD wypowiedzi | Cisza potrzebna do endpointu |
|---|---:|
| < 1,8 s | 1150 ms |
| 1,8–3,5 s | 1600 ms |
| >= 3,5 s | 2150 ms |
| głośny szum otoczenia (RMS > 850) | +250 ms, maks. 2600 ms |

Wypowiedź musi zawierać co najmniej 240 ms pozytywnych obserwacji VAD.
Pauza 300/700 ms nie jest automatycznie końcem pytania. Dłuższe wypowiedzi
dostają więcej czasu na 1500-ms przerwy. To **heurystyka czasu, bez
rozpoznawania składni lub identyfikacji rozmówcy**. Nie gwarantuje zachowania
przy każdym akcencie, TV lub niedokładnym VAD.

Przy braku wiarygodnej mowy: timeout 5 s; przy ciągłej aktywności VAD:
twardy limit jednej sesji nasłuchu 30 s; po wysłaniu końca wypowiedzi:
timeout odpowiedzi 30 s. Historyczny próg 2000 ms jest zachowany jako
`SetAdaptive(false)` do bezpiecznego A/B po wyłączeniu flagi polityki.
Nie usunięto `conversation_timeout.h`.

Po endpoint `Application` nadal:
1. czeka minimum 250 ms aż zostanie wysłana kolejka Opus (maks. 1200 ms),
2. wywołuje `protocol_->SendStopListening()` w **istniejącym** kanale,
3. czeka na odpowiedź, po timeout czyści stan i zamyka sesję,
4. kończy rozmowę po pełnym odtworzeniu PCM i wraca do keyword-only.

Nowa polityka nie zmienia tych mechanizmów ani trybów ManualStop/Realtime.

## Barge-in i bezpieczeństwo

Bez zmiany konserwatywnej ochrony pierwszych **900 ms** TTS, referencji
PCM wysyłanej do ES8311, 240-ms potwierdzenia odrębnej mowy i 400-ms
lokalnego pre-roll. Poprzednie zabezpieczenia nadal anulują stary dekoder
i blokują pakiety TTS, dopóki nie pojawi się następna transkrypcja STT.
Hostowy test potwierdza, że po pierwszym detekcie nie pojawia się drugie
przerwanie tej samej odpowiedzi. **To nie jest pełne rozwiązanie
identyfikacji pakietów sesji po stronie serwera**; konieczny test ze
spóźnionymi pakietami rzeczywistego Xiaozhi.

TV/rozmowa w tle: w domyślnym trybie wybudzania wymagane nauczone hasło,
a VAD ani energię PCM nie traktujemy jako identyfikacji osoby. Dla
`auto_speech` włączonego świadomie przez użytkownika pozostaje ryzyko
aktywacji od telewizora. Tryb „bez odbioru” nadal wymaga dotyku lub hasła.

## Współpraca z twarzą i emocjami

Gdy VAD pokazuje przerwę >= 300 ms, `Application` publikuje pojedynczy
atomowy bit `espen_turn_user_pause`. LVGL zachowuje `Phase::Listening`,
uważne oczy i nie przechodzi w neutralny stan. Processing nadal używa
`EspenTurnWaiting`. Bez nowej mimiki rozstrzygającej wyższy priorytet:
manualne trwałe emocje z telefonu i przycisk X działają jak dotychczas.
Zakończenie sesji oddaje kontrolę Living Robot Engine, ustami steruje
wyłącznie odtwarzany PCM.

## Diagnostyka

Główne zdarzenia `Ala turn: endpoint`, `stop sent` i `listen timeout`
zawierają długość mowy, ciszę, próg i szum (bez próbek audio i transkrypcji).
`AlaTurnCounters` utrzymuje w RAM liczniki startów, endpointów, timeoutów
i pauz. Odczyt na żywo przez istniejący UART/monitor logów z filtrem
`Ala turn:`; brak logów co klatkę i brak zapisu do NVS.

Metryki testów na tym etapie:
- krótkie pytanie: próg 2000 → 1150 ms (teoretycznie 850 ms mniej
  oczekiwania po ostatnim dodatnim VAD);
- wypowiedź >3,5 s: próg 2000 → 2150 ms (lepsza tolerancja pauzy);
- szum >850 RMS: +250 ms, twardy limit <2600 ms.
To **wyniki algorytmu** i testów hostowych, nie pomiary percepcji,
CPU/PSRAM ani skuteczności na fizycznej Ali.

## AEC — decyzja: nie włączać

Dokumentacja Espressif potwierdza AEC dla ESP32-P4:
- https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/acoustic_echo_cancellation/README.html
- https://docs.espressif.com/projects/esp-sr/en/latest/esp32p4/audio_front_end/README.html

ESP-SR ma warianty SR / FD / VOIP; AEC potrzebuje poprawnego, wyrównanego
w czasie kanału referencyjnego PCM (m.in. 16 kHz w standalone AEC).
Obecny barge-in ma obliczeniową referencję *głośnika*, lecz samo jej
posiadanie nie dowodzi synchronizacji ramka-do-ramki z mikrofoniem AFE.
Nie mamy pomiarów dla przypiętej wersji SDK ani fizycznego testu
CPU/PSRAM/latencji, więc **AEC wyłączone**, NSNet2 nadal wyłączone z
powodu historycznego problemu FFT. Osobny eksperyment sprzętowy ma
porównać AEC ON/OFF (ten sam poziom audio, ten sam echo/double-talk),
wykazać pobór CPU/RAM, opóźnienie AFE i brak crashy; dopiero wtedy
ewentualny PR integracyjny.

## Walidacja i wersja

Testy hostowe: `tests/test_ala_conversation_manager.cc`, rozszerzony
`tests/test_barge_in.cc`, odziedziczone testy Behavior/Face/standby/TV.
Workflow `behavior-guards.yml` uruchamia test turn policy, a
`build-pages.yml` uruchamia pełny obraz ESP-IDF i weryfikację 16 MB
dla PR kierowanego do branch #36. GitHub Actions musi **faktycznie**
skończyć się sukcesem — zielonego wyniku nie zakładamy z góry.

Firmware pozostaje 0.2.17; nie publikujemy nowego stabilnego instalatora.
Użytkownik instaluje tylko **pełny flash** (`espen.bin`), żadnej aktualizacji
częściowej. Nie scalamy stacked PR automatycznie.

### Fizyczna lista kontrolna — NIE WYKONANA

- [ ] Wystartować i przez 30 min sprawdzać brak resetów / bootloopa,
      CPU, wolny RAM/PSRAM, czas na ramkę i płynność animacji.
- [ ] 10 krótkich pytań + 10 długich wypowiedzi w ciszy; zanotować
      endpoint oraz latency do rzeczywistego startu TTS.
- [ ] Pauzy ~0,5 / 1 / 1,5 s: czy Ala nie kończy wypowiedzi w środku.
- [ ] 10 barge-in przy głośności 25/50/75%; odnotować skuteczne i fałszywe.
- [ ] 10 długich odpowiedzi własnych bez człowieka; zero self-barge.
- [ ] TV przez 10 min w keyword-only; brak niepożądanego wake.
- [ ] „Bez odbioru”, wybudzenie hasłem/dotykiem, X z telefonu,
      kontrola trwałej emocji, tryby Living ACTIVE/CALM/SLEEP.
- [ ] Test Wi-Fi disconnect / reconnect, zerwanie Xiaozhi i
      stare pakiety TTS po przerwaniu.
- [ ] Kamera, głośnik, FX, pełny obraz instalacyjny; test odsłuchowy PR #32
      dopiero po ręcznym integrowaniu.

Wymagane pomiary A/B na urządzeniu przed stwierdzeniem jakiejkolwiek
poprawy skuteczności rozpoznawania, stabilności lub średniej latencji.
