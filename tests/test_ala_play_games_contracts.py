"""Stage 10 touchscreen games must remain local and non-disruptive."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
b=(root/"firmware/espen_board.cc").read_text()
p=(root/"scripts/prepare.py").read_text()
f=(root/"firmware/ala_play_games.h").read_text()
assert '#include "ala_play_games.h"' in b
assert 'ala_play_games.h' in p
assert 'OpenPlayGames()' in b and 'lv_layer_top()' in b
assert 'game_panel_' in b and 'OverlayVisible()' in b
assert 'AlaPlayGames' in b
assert '#include <esp_' not in f and 'McpServer' not in f and 'HomeAssistant' not in f
assert 'AlaPlayGames' in f and 'PlayMode::Story' in f
print("Stage 10 game integration contracts: OK")
