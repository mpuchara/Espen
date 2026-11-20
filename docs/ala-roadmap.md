# Ala — plan rozwoju prompt po prompcie (2026-11-20)

## Cel i stan wyjściowy

Chcemy rozwinąć Alę z asystenta głosowego ESP32-P4 w ekspresyjnego, społecznego robota domowego, bez utraty stabilności ani prywatności.

**Źródło prawdy:** aktualny `main` projektu `mpuchara/Espen`, nie sam numer wersji w README. W chwili przygotowania planu `main` zawiera instalator pełnego obrazu 0.2.17 oraz pilot przez telefon. Otwarte PR-y dotyczą m.in. emocji (0.2.18), panelu telefonu i ekspresji. Najpierw sprawdzamy nakładanie się zmian, a nie implementujemy ponownie istniejących funkcji.

Istniejące komponenty, które należy zachować:
- `firmware/face_engine.h` — 16 emocji, wejścia/loop animacji, autonomiczne mruganie i spojrzenia, reakcje na dotyk; nie pisać drugiego silnika twarzy.
- `firmware/emotional_audio.h`, `response_reactions.h`, `social_reactions.h` — odgłosy i reakcje wypowiedzi; nie tworzyć drugiego toru I2S/PCM.
- `firmware/barge_in.h`, `speech_onset.h`, `speech_quality.h`, `standby_policy.h` — istniejąca logika rozmów i trybu „bez odbioru”.
- `firmware/espen_camera.*`, `firmware/camera/*` — kamera, podgląd, analiza jednorazowa.
- `firmware/phone_panel.h`, `web/phone.html`, `android/*` — pilot telefonu; zachować ręczne wymuszenie emocji i możliwość X.
- `tests/*`, `scripts/build.sh` — host tests i kompilacja ESP-IDF/Xiaozhi.

## Niezmienne zasady techniczne

1. **Jedno zadanie na prompt i jeden PR.** Każdy PR zaczyna się od aktualnego `main`, obejmuje testy i kryteria akceptacji; po review/merge kolejny prompt. Brak wyprzedzającego łączenia niezależnych funkcji.
2. **Bez regresji:** nadal działają Wi-Fi przez C6/SDIO, Xiaozhi, kamera, polski TTS, głośnik, panel telefonu, lokalne hasło, „bez odbioru” i barge-in. Nie uruchamiać dodatkowego AFE/I2S, blokujących opóźnień ani sieci z zadania LVGL.
3. **Czysta logika w lekkich modułach C++17** bez dynamicznych alokacji w gorącym przebiegu. Animacje obliczać z istniejącego zegara LVGL; dostarczanie zdarzeń przez bezpieczną kolejkę/mailbox, bez bezpośredniego UI z innych zadań.
4. **Priorytety reakcji:** bezpieczeństwo/prywatność i „bez odbioru” > polecenie użytkownika i manualna emocja z pilota > aktywna sesja słuchania/TTS > reakcja kontekstowa > autonomiczne zachowanie. Długotrwała emocja z pilota nie może być samowolnie nadpisywana.
5. **Prywatność:** domyślnie brak stałej wysyłki mikrofonu lub kamery; każde rozpoznawanie mówcy/twarzy i „rozmowa grupowa” musi mieć świadome włączenie, wyraźny wskaźnik i przełącznik wyłączenia. Brak przechowywania surowych nagrań głosu, chyba że użytkownik włączy krótką diagnostykę. Pamięć podlega usunięciu/eksportowi.
6. **Dzieci:** neutralne, bezpieczne i niewywierające presji zachowanie; sympatia i życzliwość zamiast sugestii romantycznej relacji/wyłączności; bez nieproszonych prywatnych konwersacji z dzieckiem i bez profilowania wrażliwych cech.
7. **Wersja i instalator:** user używa pełnej instalacji; publikowany obraz `espen.bin` musi pochodzić z tego samego commitu co testy i manifest. Pełna instalacja może wymazać NVS (Wi-Fi, Xiaozhi, parowanie pilota, imię, hasło). Każdy etap ostrzega o tej konsekwencji i opisuje sposób ponownej konfiguracji; nie obiecywać zachowania danych.
8. **Test fizyczny jest odrębny od CI:** CI nie dowodzi poprawności audio, kamery, przerywania ani stabilności na płytce.

## Sekwencja wdrożenia

| Prompt | Funkcja / rezultat jednego PR | Główne obszary | Minimalna akceptacja |
| --- | --- | --- | --- |
| **0** | Audyt repo i ten plan; bez zmian runtime | `docs/ala-roadmap.md` | Dokument w repo; jawne zależności i ryzyka |
| **1** | Ustalenie baseline: testy hostowe, rozdzielenie wersji i istniejących feature flags, diagnostyka regresji | `scripts/build.sh`, `tests/*`, CI | PR przechodzi testy; zero zmian GPIO/SDIO/audio |
| **2** | Silnik stanu emocjonalnego (state machine z TTL, wygaszaniem, priorytetami i manual override) **bez automatycznego mówienia** | nowy mały moduł + `face_engine.h` | Deterministyczne testy timeoutów i priorytetów, brak nadpisywania emocji z panelu |
| **3** | „Żywa” Ala w ciszy: mikroruchy oczu, spojrzenia, cykl dzień/noc, spokojne zasypianie i dyskretna mimika | `face_engine.h`, UI | Animacje płynne; bez dodatkowego wątku; w trybie ciszy brak spontanicznego dźwięku |
| **4** | Niezawodny dialog: wykrywanie końca tury i barge-in, regresje telewizora, test AEC *tylko jeśli* benchmark daje zysk | `barge_in.h`, `speech_quality.h`, audio | 10 rozmów + 10 przerwań bez bootloop; TV nie wybudza w trybie hasła |
| **5** | Inteligencja społeczna v1: osobny **opt-in** tryb rozmowy grupowej; krótkie riposty, limity i „nie wtrącaj się” | bramka sesji, Xiaozhi, UI | Bez opt-in zero wtrąceń; max 1 riposta/okno; TV ograniczony, brak deklaracji 100% rozpoznania mówcy |
| **6** | Pamięć kontekstu v1: tymczasowa pamięć sesji i jawne „zapamiętaj/usuń”, później opcjonalny domowy serwer | lekki API kontrakt + lokalny serwer/bridge | pamięć wyłączalna; brak zapisu prywatnych transkrypcji bez zgody; usuwanie działa |
| **7** | Rozpoznawanie domowników po twarzy — lokalny proof-of-concept + profile i zgoda | kamera, osobny model, UI | niska liczba fałszywych powitań w teście; bez cloud upload, przycisk usuń profil; fallback bez rozpoznania |
| **8** | Integracja Home Assistant — najpierw odczyt sensorów, następnie jawnie autoryzowane akcje | lokalny REST/HA bridge, MCP | żadnych nieautoryzowanych zmian stanu; wyłączony internetowy dostęp panelu |
| **9** | Gry i zabawy: zagadki, quizy, kalambury; lokalna logika i deterministyczne wyniki | mały game engine, UI/TTS | 3 gry end-to-end, działa bez chmury gdy nie potrzeba generowania tekstu |
| **10** | Muzyka/taniec: animacja synchroniczna z dźwiękiem i klaskanie, bez nadmiernej aktywności | istniejący PCM, twarz | synchronizacja i wyciszenie; tryb spokojny, brak ciągłego nagrywania |
| **11** | Rozmowa AI z AI: imiona, kolejka tur, jawne oddawanie głosu i limit pętli | lokalny protokół tekstowy | 2 asystentów nie przerywa sobie; stop/timeout; bez nasłuchiwania własnego TTS jako komendy |
| **12** | Integracja końcowa, długi soak-test, instrukcja instalacji i rollback | CI, docs, release | ≥100 tur testowych bez crashy, checklisty sprzętowe, pełny installer spójny z buildem |

## Kryteria przejścia między promptami

- Kod + testy + dokumentacja w **jednym** PR; porównanie względem aktualnego `main`, bez podpinania się do niepołączonego PR.
- Host tests: `tests/test_face_engine.cc`, `test_social_reactions.cc`, `test_barge_in.cc`, `test_emotional_audio.cc`, `test_phone_panel.py` i odpowiednie nowe przypadki.
- Build Actions w kontenerze ESP-IDF, weryfikacja manifestu i binarki. Nie podawać instalatora z nieudanego builda.
- Przed firmware na sprzęcie: test odtwarzania PCM i restartu, Wi-Fi reconnect, kamerki, 10 polskich dialogów, polecenie „bez odbioru”, tryb hasła z TV w tle, pilot telefonu.
- Każda nowa funkcja powinna mieć wyłącznik i fallback do poprzedniego stabilnego zachowania. W przypadku regresji: rollback do poprzedniego działającego pełnego obrazu + ponowne parowanie, jeśli NVS zostało wymazane.

## Ryzyka i zależności

- **Otwarte równoległe PR-y** (zwłaszcza 0.2.18 emocje i panel telefonu): należy najpierw sprawdzić merge/conflicts; PR 2/3 nie może w ciemno nadpisać funkcji, które rozwijane są na innych gałęziach.
- **Mikrofon pojedynczy, TV i echo:** VAD wykrywa mowę, nie osobę. Tryb grupowy pozostaje eksperymentalny; alternatywa to przycisk „wtrąć się” albo wybudzanie hasłem.
- **AEC/NSNet2:** dokumentowane wcześniejsze wyjątki FFT; nie włączać historycznie problematycznych modeli bez izolowanego benchmarku i planu powrotu.
- **Pamięć/rozpoznawanie twarzy:** wymagają oddzielnej analizy pamięci PSRAM, czasu inferencji, dostępu do danych i zgody wszystkich zainteresowanych; ESP32-P4 ma ograniczone zasoby.
- **Spontaniczne głosowe zaczepki:** domyślnie wyłączone, z ciszą nocną i limitem na godzinę, żeby robot nie przeszkadzał.
- **Instalacja full-only:** plan nie zakłada ani nie wymusza OTA. Weryfikować pełną partycję 16 MiB i poprawność konfiguracji po czystym flashu.

## Konkretne polecenie dla Promptu 1

> Pracujemy w mpuchara/Espen. Bazuj na aktualnym main, nie na niewłączonych PR-ach. Najpierw sprawdź ostatni wynik GitHub Actions i zgodność numeru wersji w firmware/manifestach/instalatorze, a następnie dołóż niedestrukcyjne testy regresji dla: manualnej emocji z pilota i X, standby „bez odbioru”, TV-safe hasła oraz istniejącego silnika twarzy. Nie włączaj żadnych nowych funkcji runtime, nie ruszaj GPIO, SDIO, kodeka, AFE ani formatu NVS. Napraw tylko realnie wykryte błędy testów i rozjazd wersji, nie masowo formatuj. Uruchom host tests, GitHub build, udokumentuj wyniki; przygotuj jeden PR i podaj link oraz SHA. Jeśli CI nie jest zielone, jawnie nazwij blokadę; nie publikuj nowego obrazu do instalacji.

