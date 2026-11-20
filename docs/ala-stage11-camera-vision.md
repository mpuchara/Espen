# Ala — Stage 11: aparat → analiza obrazu → odpowiedź głosowa

**Wersja firmware:** 0.2.18. **Gałąź:** `feature/ala-stage11-tap-vision`.

## Zachowanie

- **🎮 Gry** — otwierają gry i Magiczną Historię ze Stage 10.
- **📷 Aparat** — jedno dotknięcie otwiera ekran aparatu i uruchamia **jedną**
  analizę zdjęcia: żądanie kadru OV02C10, JPEG, HTTPS do usługi Vision Xiaozhi,
  wynik na ekranie, a następnie wypowiedź Ali przez istniejący tor Xiaozhi.
  Na ekranie jest też przycisk „Opowiedz, co widzisz”, aby ponowić analizę
  świadomie, bez ciągłego nagrywania ani automatycznego wysyłania kolejnych zdjęć.
- **🎤 Mikrofon** — wraca do rozmowy, budzi ze stanu „bez odbioru” lub
  rozpoczyna/zatrzymuje odsłuch przez istniejącą logikę Xiaozhi.
- Pasek trzech przycisków znajduje się pod czatem i jest niezależny od mimiki
  robota; dawny przycisk GRY na twarzy nie wraca.

## Bezpieczeństwo i ograniczenia

Każde dotknięcie Aparat udziela **jednorazowej, maks. 45-sekundowej zgody**
na zdjęcie. Zgoda jest konsumowana atomowo przy próbie wykonania zdjęcia.
Używany jest tylko zweryfikowany endpoint **HTTPS** Xiaozhi, bez fallbacku
HTTP. Gdy wysyłanie zdjęć zostało wyłączone przez użytkownika, Wi-Fi jest
niedostępne lub Xiaozhi nie podał endpointu, nie wykonujemy transferu i
pokazujemy czytelny powód.

Kamera i upload działają w osobnym zadaniu FreeRTOS. LVGL odczytuje jedynie
wynik tekstowy przez mutex; nie blokujemy dotyku ani animacji. Zapisanych
zdjęć nie ma w NVS ani flash, a ewentualna miniaturka do rozmowy pochodzi z
tej samej klatki i podlega dotychczasowemu limitowi historii w PSRAM.
Dźwięk nie powstaje na osobnym TTS; wykorzystywany jest już działający
Xiaozhi. Odpowiedź głosowa zależy od aktywnego połączenia Xiaozhi i może być
parafrazą tekstu, nie dokładnym odczytem znak w znak.

Nie udajemy sukcesu, gdy brak usługi Vision, zdjęcia lub odpowiedzi.
Zadania CI sprawdzają kontrakty i budują pełny obraz ESP32-P4 (offset 0x0).
**Test na prawdziwej płytce P4, z realną kamerą i aktywnym Xiaozhi,
pozostaje wymagany przed uznaniem funkcji za stabilną.**

## Dystrybucja

W Stage 11 publikujemy artefakt GitHub Actions `ala-stage11-camera-vision-full-flash`
z `espen.bin` i sumą SHA256. Instalator Pages z `main` nie jest
nadpisywany przez draftowy branch. Po weryfikacji sprzętowej i scaleniu
można opublikować tę samą wersję jako oficjalną pełną instalację.
