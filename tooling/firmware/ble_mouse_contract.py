"""Reverse reviewed mouse/capacity hunks for older security section snapshots."""
from pathlib import Path
import json

def before_mouse_fix(text):
    changes=json.loads((Path(__file__).parent/'fixtures/contracts/ble-mouse-delta.json').read_text(encoding='utf-8'))
    for item in changes:
        if text.count(item['after'])!=1:
            raise AssertionError('BLE mouse/capacity hunk changed: review its coverage before updating the snapshot')
        text=text.replace(item['after'],item['before'],1)
    return text
