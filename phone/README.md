# Ala Panel — aplikacja Android

**Telefon** (Android 8.0+) steruje mimiką fizycznej Ali (Espen na ESP32-P4), a nie tylko animacją na telefonie. Wymaga firmware Espen **0.2.15** lub nowszego.

## Instalacja i połączenie

1. Na Alę wgraj Espen 0.2.15 poprzez [instalator Espen](https://mpuchara.github.io/Espen/), opcją **Aktualizuj Espen (zachowaj dane)**.
2. Zbuduj lub pobierz **AlaPanel-Android-debug-APK** z zakładki **Actions → Ala phone APK** i zainstaluj APK na telefonie.
3. Połącz Alę i telefon z **tą samą domową siecią Wi-Fi** (urządzenia muszą się widzieć bez izolacji klientów).
4. Na ekranie Ali przytrzymaj menu ustawień (5 sekund) → **Panel telefonu / kod parowania**. Zapisz pokazany adres IPv4 i **sześciocyfrowy kod**, który jest nowy po każdym restarcie Ali.
5. W aplikacji na telefonie wpisz IP oraz kod, wybierz **Sprawdź połączenie**.

## Emocje

Wybierz jedną z 16 emocji, np. radość, miłość, rumieńce, śmiech, zdziwienie, złość. Ustaw czas **5 s**, **47 s** lub **Na stałe**, opcjonalnie opóźnienie **5 s**. **Spokój** wyłącza wymuszoną emocję. Bez aktywnego wymuszenia Ala zachowuje swoją naturalną mimikę i reakcje chmurowe.

## Popraw i wypowiedz

Wpisz zdanie. **Popraw zdanie** stosuje podstawową normalizację, dużą literę i kropkę. Zaznacz **Pełna korekta online (LanguageTool)**, aby opcjonalnie wysłać tekst do zewnętrznej usługi korekty języka polskiego i uzyskać propozycje poprawy pisowni. Treść trafia tam **tylko po zaznaczeniu tej opcji**. Kliknij **Popraw i wypowiedz przez Alę**, aby telefon wytworzył polski głos Android TTS, przekonwertował WAV PCM16 na mono PCM16 24 kHz i wysłał dźwięk **bezpośrednio po Wi-Fi** do fizycznego głośnika Ali. Jeśli polski głos nie jest dostępny, zainstaluj język polski w ustawieniach systemowego syntezatora mowy.

Robot musi mieć włączony głośnik i nie prowadzić aktualnie rozmowy. Limit wypowiedzi: 360 znaków / do 25 s PCM. Aplikacja nie używa mikrofonu telefonu ani nie nagrywa dźwięku robota.

## Zabezpieczenia / ograniczenia

API działa wyłącznie w sieci lokalnej, bez wystawiania go do Internetu (**TCP 8787**). Żądania wymagają krótkotrwałego PIN-u z ekranu Ali. Transmisja w LAN odbywa się po **HTTP, bez TLS**: korzystaj tylko z zaufanej domowej sieci, nie przekierowuj portu 8787, nie używaj niezaufanego Wi-Fi. PIN nie jest trwałym hasłem kryptograficznym. Urządzenie nie obsługuje automatycznego odkrywania IP, ale można wybrać je ręcznie.

Wersja eksperymentalna. Wymaga testu kompilacji IDF oraz testu audio i dotyku na rzeczywistym ESP32-P4. Workflow Androida buduje APK automatycznie na PR i po scaleniu do main.
