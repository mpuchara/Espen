#!/usr/bin/env python3
"""Create fixed Polish button audio offline with eSpeak NG and FFmpeg."""
from pathlib import Path
import subprocess
import tempfile

PROMPTS = {
    "intro": "Cześć! Witaj w Magicznej Opowieści! Dzisiaj możemy przeżyć niezwykłą przygodę.",
    "intro_forest": "Możemy wybrać się do zaczarowanego lasu.",
    "intro_lake": "Możemy odkrywać tajemnice jeziora.",
    "intro_car": "Możemy wyruszyć na przygodę samochodem z mamą.",
    "intro_choice": "Wybierz miejsce, w którym zaczniemy naszą historię!",
    "ask_idea": "Super! Opowiedz mi, co ma się wydarzyć dalej!",
    "forest": "Opowiedz dziecku początek magicznej przygody w zaczarowanym lesie. Mów po polsku. Na koniec zaproponuj jeden pomysł i poczekaj na wybór.",
    "lake": "Opowiedz dziecku początek magicznej przygody nad jeziorem. Mów po polsku. Na koniec zaproponuj jeden pomysł i poczekaj na wybór.",
    "car": "Opowiedz dziecku początek magicznej przygody w podróży samochodem z mamą. Mów po polsku. Na koniec zaproponuj jeden pomysł i poczekaj na wybór.",
    "continue": "Tak, wybieram twój pomysł. Kontynuuj tę samą opowieść. Zaproponuj potem jeden kolejny pomysł i poczekaj na wybór.",
    "repeat": "Powtórz tylko swój ostatni pomysł i pytanie. Jeszcze nie kontynuuj opowieści.",
    "end": "Opowiedz krótkie zakończenie tej przygody i pożegnaj dziecko.",
    "camera": "Użyj teraz aparatu robota. Zrób jedno zdjęcie i opowiedz mi po polsku, co naprawdę widzisz na tym zdjęciu.",
}

def main():
    output = Path(__file__).resolve().parents[1] / "firmware" / "button_prompts"
    output.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory() as scratch:
        wav = Path(scratch) / "prompt.wav"
        for name, prompt in PROMPTS.items():
            subprocess.run(["espeak-ng", "-v", "pl", "-s", "148", "-w", str(wav), prompt], check=True)
            subprocess.run([
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(wav),
                "-ar", "16000", "-ac", "1", "-c:a", "libopus", "-b:a", "16k",
                "-application", "voip", "-frame_duration", "60", "-map_metadata", "-1",
                str(output / f"{name}.ogg"),
            ], check=True)

if __name__ == "__main__":
    main()
