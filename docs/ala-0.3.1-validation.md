# Ala 0.3.1-beta1

Punkt wyjścia aplikacji to PR #58, commit `12616fb`. Wzorcem obrazu i protokołu aparatu jest P4 `0.2.28` (`c5e0040`, PR #56). Porównano przejścia Magic Story, Face Engine, Behavior Engine, `response_reactions.h`, Visual World oraz aparat i MCP. Zmiana dotyczy aplikacji samodzielnych.

## Interakcje

- Fazy wprowadzenia, rozdziału i przechwytywania pomysłu blokują wszystkie przyciski mogące zmienić opowieść. Ten sam predykat steruje wyglądem i handlerami. Wybór tematu jest możliwy po zakończeniu głosu, a przejście do decyzji następuje po odtworzeniu rozdziału według pozycji AudioTrack.
- Przejście do fazy chronionej następuje synchronicznie podczas kliknięcia. Każde generowanie ma identyfikator. Warstwa sesji sprawdza właściciela gniazda, zdalny identyfikator sesji, identyfikator lokalnej tury i właściciela odtwarzania. Zdarzenia UI przechwytują również generowanie gry.
- Timeout, zerwanie połączenia i jawny Wstecz pozwalają bezpiecznie ponowić generowanie. Oczekujące klatki i zdarzenia poprzedniej tury tracą ważność. Udane fragmenty, ostatnia decyzja i kontekst pozostają w RAM.

## Aparat

- Camera API działa na dedykowanym wątku z limitem rozmiaru, kontrolą JPEG i subsamplingiem do ok. 480 px. Telefon preferuje kamerę tylną, zegarek przednią. Obrót ustawia API aparatu. Zgoda aplikacji, uprawnienie Androida i jawna czynność są wymagane.
- Firmware zna historyczny adres `http://api.xiaozhi.me/vision/explain`, a beta2 go odrzucał. 0.3.1 zamienia wyłącznie ten publiczny adres i wariant z końcowym ukośnikiem na HTTPS. Inne nieszyfrowane adresy oraz przekierowania są odrzucane.
- Starszy MCP ogłasza adres w `initialize`. Aplikacja najpierw przesyła pojedynczy JPEG, interpretuje tekst lub ograniczony JSON, a potem wysyła kanoniczne polecenie Opus i oddaje gotowy opis narzędziu `self.camera.take_photo`. Serwery ogłaszające adres dopiero w `tools/call` zachowują jednokrotny przepływ sterowany przez narzędzie. Odpowiedź głosowa przed zwróceniem rzeczywistego opisu jest odfiltrowywana.
- Próba na rzeczywistym Xiaozhi użyła syntetycznego JPEG. Backend ogłosił legacy HTTP. Po przejściu na HTTPS zwrócił HTTP 200 i JSON `success/filename/text`. Opis poprawnie wskazał czerwony samochód i niebieskie koło. Xiaozhi wywołał `self.camera.take_photo`, potem wypowiedział opis w pięciu segmentach i zwrócił 202 ramki Opus oraz `tts.stop`. To dowód kontraktu usługi, niezależny od kamery i HAL urządzenia.

## Telefon, zegarek i animacje

- Telefon składa się z karty twarzy, przewijanego natywnego widoku czatu i niezależnego dolnego paska. Twarz zajmuje celowo ok. 44% dostępnej wysokości. Czat oznacza role, zachowuje do 64 kart w RAM i wyświetla tekst fragmentami na podstawie zdarzeń Xiaozhi. Dolny pasek uwzględnia systemowe insets i IME.
- Garett korzysta z małego widoku Canvas z osobnymi hitboxami i stanami nieaktywnych kontrolek. Zejście aplikacji na drugi plan odwołuje turę, zwalnia aparat i zatrzymuje renderowanie.
- Sieć na czole jest animowaną ilustracją. Jej obwiednia wygasza efekt po rozpoczęciu odtwarzania. Vision używa impulsów skierowanych od środka. Zegarek ma mniej połączeń i impulsów.
- Pozy, efekty mimiki i reguły emocji korzystają z polityki P4. Oba APK zawierają klatki wektorowe i kostiumy pełnego katalogu Visual World, próbkowane co 100 ms z kanonicznego renderera P4 przez `android/ala-robot/tools/generate_visual_frames.cc`. W opowieści są dobierane z odtwarzanego zdania, a złożone emocje Xiaozhi mają pierwszeństwo nad regułą lokalną. Górna granica kostiumu chroni oczy. Klatki są zapętlane co 4,8 s. Eksport sceny matematycznej obejmuje ikonę podstawową i nie odtwarza osobnej wizualizacji konkretnego równania P4.

## Zakres akceptacji

Testy JVM obejmują blokadę, 20 prób wejścia, dziesięć kolejnych rozdziałów, ponowienie z poprzednim kontekstem, odrzucenie starego generowania, sceny, aliasy, JPEG i adresy. Testy na Androidzie obejmują produkcyjne handlery aktywnej opowieści, wejście głosowe i AudioTrack oraz kontrolowany HTTPS Vision z rzeczywistą wirtualną kamerą, błędem HTTP i brakiem adresu. Układ telefonu ma osobny test UI z przewijaniem i syntetycznymi insets klawiatury. Próba z rzeczywistym Vision nie używa zdjęć ani głosu fizycznego użytkownika.

Obie aplikacje są betą. Wydajność, zgodność orientacji konkretnej kamery, instalator producenta oraz pełny przebieg z aktywacją agenta wymagają weryfikacji na fizycznym telefonie i Garett. Dla pakietów wydanych wcześniej z innym certyfikatem aktualizacja w miejscu jest blokowana przez Androida.
