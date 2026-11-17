# The built-in button prompts: which of Kenney's Input Prompts (CC0, thirdparty/) shows each
# control, in each style. SCsub embeds them in the engine from this list, and
# `python3 prompt_images.py <Kenney's unzipped packs>` copies the files it names into thirdparty/.
#
# A control is family/control, the names InputBindings uses (input_bindings.cpp):
# - pads (xbox, playstation4, playstation5, nintendo, steam_deck): Godot's joypad buttons by
#   name, the sticks' directions, the triggers and whole-control icons (dpad, left_stick).
#   Buttons are by position, as Godot reads them: "a" is the bottom face button, which a
#   PlayStation pad prints as a cross and a Nintendo one as B.
# - keyboard: Godot's key names, lower case, spaces as underscores ("space", "kp_enter").
# - mouse: its buttons, the wheel and moving it.
#
# The standard style is Kenney's Input Prompts (vector); the pixel style is Kenney's Input
# Prompts Pixel 16x, tiles of its packed sheet (wide keys span two or three tiles, side by
# side). Generic pads (unknown ones) show the Xbox set: SDL reads them in its layout.

import os
import shutil
import sys

STANDARD_FOLDERS = {
    "xbox": "Xbox Series",
    "playstation4": "PlayStation Series",
    "playstation5": "PlayStation Series",
    "nintendo": "Nintendo Switch",
    "steam_deck": "Steam Deck",
    "keyboard": "Keyboard & Mouse",
    "mouse": "Keyboard & Mouse",
}


def _pad(prefix, face, shoulders, sticks, extra):
    controls = {
        "a": face[0],
        "b": face[1],
        "x": face[2],
        "y": face[3],
        "left_shoulder": shoulders[0],
        "right_shoulder": shoulders[1],
        "left_trigger": shoulders[2],
        "right_trigger": shoulders[3],
        "dpad": prefix + "_dpad",
        "dpad_up": prefix + "_dpad_up",
        "dpad_down": prefix + "_dpad_down",
        "dpad_left": prefix + "_dpad_left",
        "dpad_right": prefix + "_dpad_right",
    }
    for side in ("l", "r"):
        stick = ("left" if side == "l" else "right") + "_stick"
        controls[stick] = sticks + "_stick_" + side
        controls[stick + "_press"] = sticks + "_stick_" + side + "_press"
        for direction in ("up", "down", "left", "right"):
            controls[stick + "_" + direction] = sticks + "_stick_" + side + "_" + direction
    controls.update(extra)
    return controls


STANDARD = {
    "xbox": _pad(
        "xbox",
        ["xbox_button_a", "xbox_button_b", "xbox_button_x", "xbox_button_y"],
        ["xbox_lb", "xbox_rb", "xbox_lt", "xbox_rt"],
        "xbox",
        {
            "back": "xbox_button_view",
            "start": "xbox_button_menu",
            "guide": "xbox_guide",
            "misc1": "xbox_button_share",
            "paddle1": "xbox_elite_paddle_top_right",
            "paddle2": "xbox_elite_paddle_top_left",
            "paddle3": "xbox_elite_paddle_bottom_right",
            "paddle4": "xbox_elite_paddle_bottom_left",
        },
    ),
    "playstation4": _pad(
        "playstation",
        ["playstation_button_cross", "playstation_button_circle", "playstation_button_square", "playstation_button_triangle"],
        ["playstation_trigger_l1", "playstation_trigger_r1", "playstation_trigger_l2", "playstation_trigger_r2"],
        "playstation",
        {
            "back": "playstation4_button_share",
            "start": "playstation4_button_options",
            "touchpad": "playstation4_touchpad",
            "left_stick_press": "playstation_button_l3",
            "right_stick_press": "playstation_button_r3",
        },
    ),
    "playstation5": _pad(
        "playstation",
        ["playstation_button_cross", "playstation_button_circle", "playstation_button_square", "playstation_button_triangle"],
        ["playstation_trigger_l1", "playstation_trigger_r1", "playstation_trigger_l2", "playstation_trigger_r2"],
        "playstation",
        {
            "back": "playstation5_button_create",
            "start": "playstation5_button_options",
            "misc1": "playstation5_button_mute",
            "touchpad": "playstation5_touchpad",
            "left_stick_press": "playstation_button_l3",
            "right_stick_press": "playstation_button_r3",
            "paddle1": "playstation5_elite_rb",
            "paddle2": "playstation5_elite_lb",
            "paddle3": "playstation5_elite_fn_r",
            "paddle4": "playstation5_elite_fn_l",
        },
    ),
    # By position: the bottom button ("a") says B on a Nintendo pad, the right one A.
    "nintendo": _pad(
        "switch",
        ["switch_button_b", "switch_button_a", "switch_button_y", "switch_button_x"],
        ["switch_button_l", "switch_button_r", "switch_button_zl", "switch_button_zr"],
        "switch",
        {"back": "switch_button_minus", "start": "switch_button_plus", "guide": "switch_button_home"},
    ),
    "steam_deck": _pad(
        "steamdeck",
        ["steamdeck_button_a", "steamdeck_button_b", "steamdeck_button_x", "steamdeck_button_y"],
        ["steamdeck_button_l1", "steamdeck_button_r1", "steamdeck_button_l2", "steamdeck_button_r2"],
        "steamdeck",
        {
            "back": "steamdeck_button_view",
            "start": "steamdeck_button_options",
            "guide": "steamdeck_button_guide",
            "misc1": "steamdeck_button_quickaccess",
            "paddle1": "steamdeck_button_r4",
            "paddle2": "steamdeck_button_l4",
            "paddle3": "steamdeck_button_r5",
            "paddle4": "steamdeck_button_l5",
        },
    ),
    "mouse": {
        "mouse": "mouse",
        "left": "mouse_left",
        "right": "mouse_right",
        "middle": "mouse_scroll",
        "wheel_up": "mouse_scroll_up",
        "wheel_down": "mouse_scroll_down",
        "back": "mouse_side_back",
        "forward": "mouse_side_forward",
        "move": "mouse_move",
    },
}

# Keyboard keys: Godot's name for the key -> Kenney's.
_KEYS = {k: k for k in "abcdefghijklmnopqrstuvwxyz0123456789"}
_KEYS.update({f"f{n}": f"f{n}" for n in range(1, 13)})
_KEYS.update(
    {
        "escape": "escape",
        "tab": "tab",
        "shift": "shift",
        "ctrl": "ctrl",
        "alt": "alt",
        "meta": "win",
        "space": "space",
        "enter": "enter",
        "kp_enter": "numpad_enter",
        "kp_add": "numpad_plus",
        "backspace": "backspace",
        "delete": "delete",
        "insert": "insert",
        "home": "home",
        "end": "end",
        "pageup": "page_up",
        "pagedown": "page_down",
        "capslock": "capslock",
        "numlock": "numlock",
        "scrolllock": "scroll_lock",
        "print": "printscreen",
        "pause": "pause",
        "up": "arrow_up",
        "down": "arrow_down",
        "left": "arrow_left",
        "right": "arrow_right",
        "minus": "minus",
        "equal": "equals",
        "plus": "plus",
        "comma": "comma",
        "period": "period",
        "slash": "slash_forward",
        "backslash": "slash_back",
        "semicolon": "semicolon",
        "apostrophe": "apostrophe",
        "quoteleft": "tilde",
        "bracketleft": "bracket_open",
        "bracketright": "bracket_close",
        "asterisk": "asterisk",
        "any": "any",
    }
)
STANDARD["keyboard"] = {key: "keyboard_" + name for key, name in _KEYS.items()}

# The pixel style: tiles of tilemap_packed.png (34 across, 16 by 16), the light ones.
_PIXEL_STICKS = {}
for _side, _start in (("left", 416), ("right", 484)):
    _PIXEL_STICKS[_side + "_stick"] = [_start]
    for _offset, _direction in enumerate(("up", "right", "down", "left")):
        _PIXEL_STICKS[f"{_side}_stick_{_direction}"] = [_start + 1 + _offset]
    _PIXEL_STICKS[_side + "_stick_press"] = [_start + 8]
_PIXEL_DPAD = {"dpad": [41], "dpad_up": [35], "dpad_left": [36], "dpad_down": [37], "dpad_right": [38]}


def _pixel_pad(face, shoulders, extra):
    controls = {"a": [face[0]], "b": [face[1]], "x": [face[2]], "y": [face[3]]}
    for name, tile in zip(("left_shoulder", "right_shoulder", "left_trigger", "right_trigger"), shoulders):
        controls[name] = [tile]
    controls.update(_PIXEL_DPAD)
    controls.update(_PIXEL_STICKS)
    controls.update({name: [tile] for name, tile in extra.items()})
    return controls


_PLAYSTATION_PIXEL = _pixel_pad([567, 563, 565, 561], [631, 632, 629, 630], {"back": 618, "start": 617, "touchpad": 702})
PIXEL = {
    "xbox": _pixel_pad([4, 5, 6, 7], [553, 554, 551, 552], {"back": 616, "start": 617, "guide": 614}),
    "playstation4": _PLAYSTATION_PIXEL,
    "playstation5": _PLAYSTATION_PIXEL,
    "nintendo": _pixel_pad([14, 13, 16, 15], [627, 628, 625, 626], {"back": 693, "start": 694, "guide": 682}),
    "steam_deck": _pixel_pad(
        [13, 14, 15, 16], [631, 632, 629, 630], {"back": 616, "start": 617, "paddle1": 774, "paddle2": 772, "paddle3": 775, "paddle4": 773}
    ),
    "mouse": {"mouse": [76], "left": [77], "right": [78], "middle": [79], "wheel_up": [80], "wheel_down": [81]},
}
_PIXEL_KEYS = {}
for _row, _start in (("qwertyuiop", 85), ("asdfghjkl", 120), ("zxcvbnm", 155), ("1234567890", 51)):
    for _i, _key in enumerate(_row):
        _PIXEL_KEYS[_key] = [_start + _i]
_PIXEL_KEYS.update({f"f{n}": [17 + n] for n in range(1, 13)})
_PIXEL_KEYS.update(
    {
        "escape": [17],
        "quoteleft": [30],
        "minus": [61],
        "plus": [62],
        "equal": [63],
        "backspace": [66, 67],
        "bracketleft": [95],
        "bracketright": [96],
        "backslash": [99],
        "apostrophe": [129],
        "semicolon": [132],
        "enter": [134, 135],
        "up": [166],
        "right": [167],
        "down": [168],
        "left": [169],
        "slash": [165],
        "alt": [187, 188],
        "tab": [189, 190],
        "delete": [191, 192],
        "end": [193, 194],
        "numlock": [195, 196],
        "period": [197],
        "ctrl": [221, 222],
        "capslock": [223, 224],
        "home": [225, 226],
        "pageup": [227, 228],
        "pagedown": [229, 230],
        "comma": [231],
        "space": [235, 236, 237],
        "shift": [255, 256],
        "insert": [257, 258],
        "print": [259, 260],
        "scrolllock": [261, 262],
        "pause": [263, 264],
    }
)
PIXEL["keyboard"] = _PIXEL_KEYS
PIXEL_COLUMNS = 34
PIXEL_TILE = 16

THIRDPARTY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "thirdparty", "kenney_input_prompts")


def standard_files():
    """Every standard image used: (family, control, path under thirdparty/standard)."""
    for family, controls in STANDARD.items():
        for control, name in controls.items():
            yield family, control, name + ".svg"


def vendor(standard_pack, pixel_pack):
    """Copies the files this list names from Kenney's unzipped packs into thirdparty/."""
    standard = os.path.join(THIRDPARTY, "standard")
    shutil.rmtree(THIRDPARTY, ignore_errors=True)
    os.makedirs(standard)
    for family, _control, file in standard_files():
        source = os.path.join(standard_pack, STANDARD_FOLDERS[family], "Vector", file)
        shutil.copy(source, os.path.join(standard, file))
    os.makedirs(os.path.join(THIRDPARTY, "pixel"))
    shutil.copy(os.path.join(pixel_pack, "Tilemap", "tilemap_packed.png"), os.path.join(THIRDPARTY, "pixel", "tilemap_packed.png"))
    shutil.copy(os.path.join(standard_pack, "License.txt"), os.path.join(THIRDPARTY, "License.txt"))


if __name__ == "__main__":
    vendor(sys.argv[1], sys.argv[2])
