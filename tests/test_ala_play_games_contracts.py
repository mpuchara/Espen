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
assert 'StorySetting::Lake' in f and 'StorySetting::Car' in f
assert 'Nad jeziorem' in f and 'Z mamą w samochodzie' in f
assert 'BuildBottomDock()' in b and 'DrawDockIcon(e,DockIcon::Play)' in b
assert 'DrawDockIcon(e,DockIcon::Camera)' in b and 'DrawDockIcon(e,DockIcon::Mic)' in b
assert 'lv_obj_set_style_pad_bottom(content_,88,0)' in b
assert 'game_repeat_' in b and 'OpenStoryThemes()' in b and 'PumpMagicStory()' in b
phone=(root/"scripts/prepare_phone.py").read_text()
assert 'EspenGenerateMagicStory' in phone
assert 'SendWakeWordDetected(prompt)' in phone
assert 'self->Hide(self->game_panel_,true)' in b
assert 'auto* play=MakeButton(face_,"GRY"' not in b
print("Stage 10 game integration contracts: OK")
