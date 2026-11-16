"""Parse Arduino input frames and apply them to a virtual Xbox 360 pad."""

from __future__ import annotations

from typing import NamedTuple


AXIS_LIMIT = 1000
BUTTON_NAMES = ("XUSB_GAMEPAD_A", "XUSB_GAMEPAD_B", "XUSB_GAMEPAD_X", "XUSB_GAMEPAD_Y")


class ControllerFrame(NamedTuple):
    axes: tuple[int, int, int, int]
    buttons: tuple[bool, bool, bool, bool]


def parse_frame(line: bytes) -> ControllerFrame | None:
    """Read J1 X/Y, J2 X/Y and A/B/X/Y from one current firmware frame."""
    try:
        text = line.decode("ascii").strip()
        if text.startswith("joystick:"):
            text = text[len("joystick:") :]
        values = [int(value) for value in text.split(",")]
        if len(values) < 14:
            return None
        axes = tuple(max(-AXIS_LIMIT, min(AXIS_LIMIT, value)) for value in values[:4])
        buttons = tuple(value == 1 for value in values[10:14])
        return ControllerFrame(axes, buttons)
    except (UnicodeDecodeError, ValueError):
        return None


def normalized(value: int, invert: bool = False) -> float:
    result = max(-AXIS_LIMIT, min(AXIS_LIMIT, value)) / AXIS_LIMIT
    return -result if invert else result


def apply_frame(gamepad, vgamepad, frame: ControllerFrame, invert_left_y=False, invert_right_y=False) -> None:
    j1x, j1y, j2x, j2y = frame.axes
    gamepad.left_joystick_float(
        x_value_float=normalized(j1x),
        y_value_float=normalized(j1y, invert_left_y),
    )
    gamepad.right_joystick_float(
        x_value_float=normalized(j2x),
        y_value_float=normalized(j2y, invert_right_y),
    )
    for button_name, pressed in zip(BUTTON_NAMES, frame.buttons):
        button = getattr(vgamepad.XUSB_BUTTON, button_name)
        if pressed:
            gamepad.press_button(button=button)
        else:
            gamepad.release_button(button=button)
    gamepad.update()


def reset_gamepad(gamepad) -> None:
    gamepad.reset()
    gamepad.update()

# by codex
