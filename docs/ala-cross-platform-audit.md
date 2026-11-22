# Audyt Ali na ESP32-P4, Androidzie i Garett Essa

## Punkty odniesienia

- `main` 43cba1a zawiera mobilne 0.3.0-beta1 z [PR #57](https://github.com/mpuchara/Espen/pull/57). Aplikacje mają osobne identyfikatory instalacji i kompilują te same klasy z `android/ala-robot/watch`. `android/app` to odrębny Pilot. Wcześniejsze gałęzie mobilne to `feature/garett-espen-watch` (360269c) i `feature/espen-mobile-android-mvp-20261117` (#19), a #22/#23/#25 dotyczą Pilota.
- Wzorzec firmware’a to c5e0040 z [PR #56](https://github.com/mpuchara/Espen/pull/56), w stosie #55 → #54 → #53 → #52 → #50 → #47 → #45 → #43 → #42 → #41 → #38 → #37 → #36 → #35, z dołączonym #44. Audyt Stage 9 obejmuje również przywrócenie zmian z #32 i cichy start nagrywania (#49). Otwarte #39 i #40 są alternatywnym torem autonomii. #20/#21/#34 dotyczą wcześniejszych reakcji lub testów, #33 jest planem, a #46 dotyczy Home Assistant. Nie trzeba scalać tych gałęzi, by przenieść zachowanie.
- [CI firmware’u 0.2.28](https://github.com/mpuchara/Espen/actions/runs/45010169213) i [CI mobilnego beta1](https://github.com/mpuchara/Espen/actions/runs/45014420079) przeszły. Log COM7 wcześniejszej instalacji potwierdza uruchomienie 0.2.28, odtwarzanie, trzy korekcje kolejności UDP oraz wystąpienia timeoutu odpowiedzi. Pełny odsłuch kontynuacji w 0.2.28 pozostaje niepotwierdzony. CI mobilne sprawdzało JVM, budowę i podpis. ADB nie wykrył podłączonych urządzeń.

## Macierz wejściowa 0.3.0-beta1

| Funkcja | ESP32-P4 0.2.28 | Android | Garett | Zgodność | Wymagane poprawki |
|---|---|---|---|---|---|
| Polityka zachowania | `ala_behavior_engine.h`, priorytety bezpieczeństwa, dotyku, chmury, nastroju | Rozproszony stan widoku | Tak samo | Niepełna | Wspólna deterministyczna polityka i mapowanie faz |
| Autonomia i sen | Losowane gesty zależne od aktywności i pory, sen i wybudzanie | Losowe przesunięcia; oczy zamykają się po 25 s od dotyku, także zaraz po starcie | Tak samo | Błędna | Czasy i gesty ze wzorca, reset po interakcji |
| Mimika i emocje | `face_engine.h`, 16 nastrojów, pozy, efekty, przejścia | Kilka warunków tekstowych, inne proporcje i animacje | Tak samo | Niepełna | Użyć poz i aliasów firmware’u; animować zgodnie z fazą |
| Osobowość i reakcje społeczne | Ten sam agent, `social_reactions.h`, odpowiedzi i mowa emocjonalna | Agent przypisywany ręcznie, głównie emocje LLM | Tak samo | Niepełna | Zweryfikować parowanie i przenieść lokalne reakcje |
| Rozmowa / STT | Xiaozhi, AFE, adaptacyjny koniec i barge-in | MIC 16 kHz / Opus, prosty RMS, ponowne dotknięcie zamyka również nagrywanie | Tak samo, inny HAL | Niepełna | Rozdzielić koniec wypowiedzi od anulowania, ograniczyć czasy |
| Transport | MQTT/UDP; porządkowanie, zapas audio | Uporządkowane ramki WebSocket, kolejka 24 z usuwaniem najstarszych | Tak samo | Błędna obsługa przeciążenia | Ograniczyć bufor, wykrywać przeciążenie i zgłaszać niepełną odpowiedź |
| Głośnik i usta | Odniesienie do PCM z toru wyjściowego, zakończenie po drenażu | RMS przed zapisem, timer 850 ms, sukces nawet bez audio, wspólna flaga wątków | Tak samo, HAL 8.1 | Błędna | Zegar odtwarzania, zwolnienie wyjścia i wykrycie niepełnej tury |
| Gry | `ala_play_games.h`, własne pytania, podpowiedzi, ocena i reakcje | Inny zestaw pytań, lokalny TTS opcji | Tak samo; polski TTS może być niedostępny | Niepełna / ograniczenie Androida | Port danych i reguł, jeden właściciel głosu |
| Magiczna Opowieść | Głosowe komendy Opus, MCP `self.story.read_context`, widoczna twarz | `listen.detect` do rozdziałów, lokalne menu, uproszczony kontekst | Tak samo | Błędna / niepełna | Standardowa tura audio oraz kontekst z ograniczeniami i ponowieniem |
| Vision | Zdjęcie na żądanie, analiza, osobna odpowiedź Xiaozhi | Zdjęcie w RAM, handoff przez `listen.detect`, MCP i HTTPS | Tak samo; kamera zależna od urządzenia | Niepełna | Standardowa tura audio, ważna zgoda i unieważnianie wyniku |
| Pamięć i profile | Pamięć NVS opt-in, PIN, jawny odczyt dla AI | Tylko nietrwały stan historii | Tak samo | Brak implementacji | Port polityki zgody i pamięci; sprawdzić zachowanie po restarcie |
| Rozpoznawanie użytkownika | Eksperymentalna twarz MFN w RAM, głos i automatyczne TTS społecznościowe zablokowane warunkami | Brak | Brak | Brak implementacji / różnice modeli i HAL | Lokalny model opt-in i fizyczna kalibracja, zachować fail-closed |
| Mikrofon podczas bezczynności | Tryby rozmowa/hasło/wyłączony, słowo budzące lokalne | Tylko ręczny start, brak procesu w tle | Tak samo | Brak lokalnego wake word | Oddzielić lokalne wybudzenie od transmisji, zweryfikować koszt |
| Cykl życia | Zegar aplikacji i lokalny arbitraż | Wstrzymanie z kolejki, nieczyści trwającej gry, brak ochrony przed wyścigiem | Tak samo, dodatkowo wygaszenie | Błędna | Skończyć sesję i oddać zasoby podczas pauzy, odrzucić stare callbacki |
| Układ i instalacja | Pion 480×800, dolny pasek | Przeskalowane 360×400, min API 26 / target 35 | 360×400 logicznie, min 26 / target 27, v1 | Niepełna / ograniczenie sprzętowe | Adaptacja twarzy i hitboxów; rzeczywista instalacja i pomiary obu urządzeń |

Wiersze macierzy wejściowej wynikają z kodu. Weryfikacja działania jest opisana osobno poniżej.

## Macierz kandydata 0.3.0-beta2

| Funkcja | Telefon i Garett | Stan względem ESP32-P4 0.2.28 |
|---|---|---|
| Pozy i aliasy emocji | 16 nastrojów na wspólnym Canvas, kolory, płynne przejścia, efekty i aktywność | Zgodne dane poz; częściowa zgodność animacji i geometrii. Brak automatycznego porównania klatka po klatce z LVGL |
| Osobowość i reakcje | Ten sam agent, reakcje na jawną wypowiedź, pukanie, pogłaskanie i łaskotanie; nastroje i wygaszanie gestów w grze | Częściowa. Brakuje profili osobowości, efektów progresji i toru specjalnych dźwięków emocji z firmware’u |
| Bezczynność i sen | Aktywność lokalna, sen zależny od pory, wybudzenie dotykiem | Częściowa. Brak lokalnego budzenia mową i hasłem oraz trybów nasłuchu P4 |
| Rozmowa i Opus | Ręczny start, adaptacyjny koniec wypowiedzi według czasów P4, RMS i ograniczony czas, 16 kHz, nadawanie bez sygnału startu, wersje WebSocket 1–3, timeouty i przerwanie | Częściowa. Inna ścieżka akustyczna; adaptacyjny AFE, AEC i automatyczne barge-in z P4 pozostają poza tym portem |
| Odtwarzanie i mimika | Poziom z PCM skojarzony z pozycją odtwarzania, ograniczona kolejka, wykrywanie niepełnej tury, 12 ramek zapasu kontynuacji | Częściowa. Porządek transportu wynika z WebSocket. Opóźnienie i brak słyszalnych przerw na urządzeniach pozostają niezweryfikowane |
| Quiz i zwierzęta | Pytania i 5 rund z P4, animowana reakcja, odczyt i podświetlanie opcji, powrót | Częściowa. Głos zależy od lokalnego polskiego TTS; brakuje odtworzenia efektów dźwiękowych P4 |
| Magiczna Opowieść | Identyczne polecenia Opus. MCP i kontekst kontynuacji. Gra czeka na rzeczywiste odtworzenie i obsługuje propozycję, własne dyktowanie, ponowienie i zakończenie | Częściowa. Na ekranie pulsuje temat rozpoznany w aktualnie odtwarzanej frazie; kilka tematów w jednej frazie nie daje dokładnego wyróżnienia każdego słowa. Pełna sesja z urządzenia wymaga akceptacji |
| Vision | Zgoda jednorazowa, kamera, ograniczona pamięć, wysyłka HTTPS, MCP i mówiona odpowiedź z Xiaozhi | Częściowa. Wymaga dostępnego endpointu; oficjalny Vision i kamera Garett wymagają próby fizycznej |
| Pamięć i rozpoznawanie | Kontekst opowieści jest tylko w RAM. Brak trwałej pamięci, profili biometrycznych i modelu twarzy | Brak zgodności z funkcjami lokalnymi P4 |
| Autonomia społeczna | Lokalna mimika, mikrofon po jawnym działaniu | Brak modelu autonomii społecznej. W P4 spontaniczna wypowiedź zależy od zgody i pozostaje blokowana bez zweryfikowanego kanału |
| Układ i cykl życia | Wspólny Canvas, osobna geometria telefonu i zegarka, oddawanie zasobów po pauzie, trzy przyciski i gest ustawień | Częściowa. Zgodność z instalatorem, rzeczywistym ekranem, sterownikiem audio i aparatem Garett do sprawdzenia |

## Dowody i granice wyniku

- W powtórzonej próbie protokołu z agentem używanym przez P4, przez WebSocket v1: tekstowe `listen.detect` dało 1 komunikat `alert` i 0 ramek Opus po 32 s. Wysłanie kanonicznego `story_context.ogg` w turze `listen.start → Opus → listen.stop` dało STT, wywołanie `self.story.read_context`, osiem zdarzeń `sentence_start`, 467 ramek audio i `tts.stop`. W odpowiedzi wystąpiły postacie i decyzja przekazane w syntetycznym kontekście. Próbę wykonano klientem protokołu, bez użycia mikrofonu i bez przesyłania wypowiedzi dziecka.
- Sześć skopiowanych zasobów audio ma identyczny SHA-256 z firmware’em c5e0040. Test JVM dekoduje każdy ich pakiet jako mono Opus o długości 60 ms przy 16 kHz. Testy JVM obejmują też przejścia, aliasy i gesty, kontekst opowieści, ponowienie, ramki WebSocket i klasyfikację zgody Vision.
- JVM: 21 testów bez błędów, w tym cisza, krótka wypowiedź, dłuższa pauza oraz limit ciągłego nagrywania.
- Android 8.1 API 27: początkowy przebieg instrumentacyjny miał dwa niezaliczone scenariusze — wznowienie przekroczyło okno oczekiwania, a emulator nie oddał STT w próbie bez ręcznego zakończenia. W powtórzonym przebiegu przeszło 7/7 przypadków: 10 kolejnych tur Opus; odmowa pustej odpowiedzi z odzyskaniem; timeout i rozłączenie; zakończenie wejścia po trzech ramkach z AudioRecord i wynik STT; MCP rozdziału; anulowanie po wyjściu i wznowienie; zdjęcie z wirtualnej kamery przekazane raz do kontrolowanego HTTPS Vision. Backend testowy zwracał syntetyczne odpowiedzi, więc ten przebieg nie kwalifikuje chmury ani sprzętu producenta.
- Oba pakiety z podpisem debug zainstalowały się i uruchomiły na emulatorze Androida 8.1. Certyfikat lokalnego APK różni się od obu APK w GitHub Release beta1. Weryfikacja podpisu potwierdza poprawną paczkę, lecz aktualizacja beta1 wymaga zgodnego klucza lub oddzielnie uzgodnionej migracji ustawień i parowania.
- Wydajność: praca Canvas jest ograniczana po pauzie i na nieaktywnym oknie. Usunięto tworzenie ścieżek, gradientu, krojów pisma i prostokątów w każdej klatce. Dekoder i I/O pozostają poza wątkiem widoku; kolejka ma 160 ramek, a callbacki anulowanej tury są odrzucane. Próbka menu na emulatorze 360×400 wykazała 135 klatek, 0 klatek zaklasyfikowanych przez gfxinfo jako janky, p95 10 ms i około 15 MiB PSS. To punktowy odczyt procesu debug, bez porównywalnej próbki beta1. Brak pomiaru baterii, pamięci i stabilności na fizycznym zegarku. Nie potwierdzono też realnych strumieni mikrofonu i aparatu na fizycznych urządzeniach.
