# Ala — Etap 3: Living Robot Engine (stacked PR na #35)

## Zakres i pochodzenie

Implementacja rozwija **jedyny** `AlaBehaviorEngine` z PR #35 i
wykorzystuje dotychczasowy `FaceEngine`. Nie dodaje osobnego wątku,
silnika animacji, framebufferów, nowych bibliotek, NVS, aparatu,
rozpoznawania twarzy ani usług online. Bez zmian wersji, instalatora
i toru PCM. Wersja 0.2.17 pozostaje identyczna z bazą #35.

Gałąź Stage 3 powstała od SHA `26dde2409105d4267ea73d423f19ed8f64b9b9a2`
PR #35, a jej PR musi mieć **bazę gałęzi Etapu 2**, nie `main`.
PR #34 dostarczył podstawy testowe do #35; #32 wciąż jest odrębną
zmianą odroczonych FX (nie scalać automatycznie i przeprowadzić
odsłuch po późniejszej integracji), #33 to osobny plan.

## Polityka aktywności i priorytety

| Stan | Warunek domyślny | Twarz |
| --- | --- | --- |
| ACTIVE | Rozmowa, bezpośrednia interakcja lub pierwsze 45 s po niej | Żwawsze, ale ograniczone gesty |
| CALM | 45–210 s od interakcji | Spokojne spojrzenia, rzadziej mrugnięcia |
| SLEEP | Po 210 s, wymuszone uśpienie lub standby | Zamyka oczy, brak autonomicznych mikroanimacji |

Limity zaszyte w `LivingTiming` można regulować hostowo (bez NVS).
Przy **wiarygodnym** czasie lokalnym wieczór skraca sen do 150 s,
a noc do 90 s. W innych porach 210 s. Odpoczynek nie jest trybem
„bez odbioru”: **nie zmienia** mikrofonu, wake-word, Xiaozhi ani
`StandbyPolicy`. Przejścia Idle↔Offline nie zerują licznika bezczynności.
Stan Startup również może zasnąć, a nie wywołuje ciekawskich gestów.

Synchronizacja czasu: raz na minutę sprawdzany jest zegar systemowy.
Potrzebny jest rok po 2024 i jawny `TZ`, dla którego `localtime_r`
daje godzinę lokalną. Brak zegara lub strefy to `DayPart::Unknown`,
**bez zgadywania lokalnej godziny i bez dodatkowego serwera czasu**.
Integracja nie wymusza SNTP ani nie nadpisuje ustawień systemowych.

Wybór powtarzalnych mikroreakcji to xorshift32 z ziarnem i stanem
poprzedniej akcji. Dostępne: lewo, prawo, góra, dół, pojedyncze
i sporadyczne podwójne mrugnięcia, delikatny uśmiech, przechylenie
głowy i krótkie przymrużenie. Długość reakcji ~1.25–1.65 s,
odstępy zależą od `Persona`, stanu i pory dnia; ruchy oczu
interpoluje istniejące `FaceEngine::Approach`, max 3 jednostki / tick.
W nastroju telefonu zachowuje się **Mood / priorytet telefonu**,
dopuszczając wyłącznie nieinwazyjny ruch wizualny; nie wpycha nowej
emocji. Po wejściu w SLEEP automatyczne gesty ustają.

Po zakończeniu rozmowy afterglow wciąż utrzymuje się przez 14 sekund.
Podczas mówienia FaceEngine utrzymuje realny PCM dla ust, a gesty
autonomiczne są wyłączone. Dotyk, głaskanie, łaskotanie i wybudzenie
korzystają ze starych gestów/Social Reactions, a ich zdarzenia
resetują zegar bezczynności. `SleepRequested` wymusza SLEEP do
kolejnego dozwolonego wybudzenia; X telefonu kasuje ręczną emocję
natychmiast. Pinned mood jest pamiętany podczas wizualnego SLEEP,
ale wyświetlanie może wygasnąć po długiej bezczynności; po
wybudzeniu zapisany mood powraca. Ani noc, ani mimika nie generują
nowego odgłosu / wypowiedzi.

## Testy hostowe i CI

`tests/test_living_robot_engine.cc`: symulacja 5 minut przy ticku
50 ms, rzeczywista twarz + polityka, naturalny ruch i brak skoków,
brak identycznych reakcji pod rząd, przejścia stanów, czas nocny,
niezresetowany timer po utracie Wi-Fi, ustawione `Persona`,
powtarzalne ziarno, 32-bit wrap, pinned mood, X, PCM, standby,
dotyk i wybudzenie. Starsze testy Etapu 2 nadal działają.

Uruchomienie lokalne:
```bash
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_living_robot_engine.cc -o /tmp/ala-living-robot-test
/tmp/ala-living-robot-test
c++ -std=c++17 -O2 -Wall -Wextra -Werror tests/test_ala_behavior_engine.cc -o /tmp/ala-behavior-test
/tmp/ala-behavior-test
```

Oba testy są też w `scripts/build.sh` i szybkim workflow
`Ala behavior contracts`. `Espen firmware and installer` buduje
pełny obraz ESP32-P4 16 MB, ze standardową weryfikacją SHA.
Dla tego stacked PR workflow może działać na bazie #35,
a deploy Pages pozostaje zablokowany dla PR.

## Bramka fizyczna — **niewykonana automatycznie**

- [ ] Instalacja pełnego obrazu na Ali ESP32-P4, brak bootloopu.
- [ ] 5 minut obserwacji bez rozmowy: ACTIVE → CALM → SLEEP, ruchy przed snem.
- [ ] Płynność spojrzenia, mrugnięcia (także podwójne), naturalność uśmiechu.
- [ ] Dotyk: reakcja w stronę palca i powrót spojrzenia, brak sztucznych skoków.
- [ ] Głaskanie, łaskotanie, lekkie przechylenie, serduszka.
- [ ] Dotyk/hasło wybudza, a „bez odbioru” respektuje stary mechanizm.
- [ ] Pinned Love z panelu >1 min, brak nadpisania mimiki, X natychmiast.
- [ ] 10 rozmów Xiaozhi; PCM/lip-sync; możliwość przerwania TTS.
- [ ] TV w tle nie inicjuje sesji; bez Wi-Fi nie budzi bez końca.
- [ ] Brak niezamówionych nocnych dźwięków, sprawdzenie TZ/SNTP/fallback.
- [ ] Obciążenie CPU i pamięci, sprawność LVGL, 1+ h pracy bez resetu.
- [ ] Po połączeniu PR #32 osobny odsłuch FX i test barge-in.

Nie oznaczać obrazu jako sprzętowo zweryfikowanego ani nie publikować
jako stabilnego bez powyższej bramki. Nie scalać PR automatycznie.
