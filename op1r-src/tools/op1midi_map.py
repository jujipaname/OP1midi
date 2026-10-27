#!/usr/bin/env python3
"""OP1midi: the MIDI CC mapping of the tape marker actions (synth_sdk/midi/mm.c, OP-1 OG / op1r R246).

Actions (MVP): tape.marker.next, tape.marker.previous, tape.play_from_marker. All are trigger type:
the OP-1 runs the action once when the CC value crosses to >= 64. Marker actions act in tape mode
only; play-from-marker only plays from stop (no punch, no count-in). The OP-1 stores the mapping as
op1rsettings keys 'mmch' (0 off / 1..16 / 17 all), 'mm0' 'mm1' 'mm2' (CC numbers, 0 = unassigned) -
edited on the settings page (clock mode + T3: green channel, white NEXT, red PREV) or written by a
tool that can apply them; this one validates, prints the device values and sends test CCs.

Mapping file (strict: unknown keys / actions rejected, like the firmware loader):

  {
    "version": 1,
    "channel": 1,                                     # 1..16, or "all"
    "mappings": [
      {"action": "tape.marker.next", "cc": 80},
      {"action": "tape.marker.previous", "cc": 81},
      {"action": "tape.play_from_marker", "cc": 82}   # cc 0 = unassigned
    ]
  }

  python3 tools/op1midi_map.py check mapping.json     # validate, print the device values
  python3 tools/op1midi_map.py template               # print the default mapping
  python3 tools/op1midi_map.py test [--port OP-1] [--ch 1] [--cc 80]   # send trigger CCs (mido)
"""
import json, sys

ACTIONS = ("tape.marker.next", "tape.marker.previous", "tape.play_from_marker")
DEFAULTS = {"tape.marker.next": 80, "tape.marker.previous": 81, "tape.play_from_marker": 82}
# CCs the firmware already uses: keys (CC = keycode + 5), MIDI LFO 1-4, synth automation 53-60 /
# 72-79 (midi/mx.c), encoders 102-105, mixer 106-118 (dispatcher.c midi_cc_hook). Never map these.
RESERVED = set(range(1, 18)) | set(range(21, 27)) | set(range(38, 44)) | set(range(48, 61)) | \
           set(range(72, 80)) | set(range(102, 119))
FREE = "18-20, 27-37, 44-47, 61-71, 80-101, 119-127"


class Bad(Exception):
    pass


def load(path):
    try:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
    except OSError as e:
        raise Bad(f"cannot read {path}: {e}")
    except json.JSONDecodeError as e:
        raise Bad(f"not valid JSON: {e}")
    if not isinstance(doc, dict):
        raise Bad("top level must be an object")
    if set(doc) != {"version", "channel", "mappings"}:
        raise Bad(f"keys must be exactly version/channel/mappings, got {sorted(doc)}")
    if doc["version"] != 1:
        raise Bad("version must be 1")
    ch = doc["channel"]
    if ch == "all":
        mmch = 17
    elif isinstance(ch, int) and not isinstance(ch, bool) and 1 <= ch <= 16:
        mmch = ch
    else:
        raise Bad("channel must be 1..16 or \"all\"")
    maps = doc["mappings"]
    if not isinstance(maps, list) or len(maps) != len(ACTIONS):
        raise Bad(f"mappings must list exactly {len(ACTIONS)} actions")
    seen, ccs = set(), {}
    for m in maps:
        if not isinstance(m, dict) or set(m) != {"action", "cc"}:
            raise Bad(f"each mapping needs exactly action/cc, got {m!r}")
        act, cc = m["action"], m["cc"]
        if act not in ACTIONS:
            raise Bad(f"unknown action {act!r} (known: {', '.join(ACTIONS)})")
        if act in seen:
            raise Bad(f"duplicate action {act}")
        seen.add(act)
        if not isinstance(cc, int) or isinstance(cc, bool) or not 0 <= cc <= 127:
            raise Bad(f"{act}: cc must be 0..127")
        if cc in RESERVED:
            raise Bad(f"{act}: CC {cc} is used by the firmware itself; free ranges: {FREE}")
        if cc and cc in ccs.values():
            raise Bad(f"{act}: CC {cc} already used by {[a for a, c in ccs.items() if c == cc][0]}")
        ccs[act] = cc
    return mmch, ccs


def device_values(mmch, ccs):
    """op1rsettings rows the OP-1 stores (screens/settings.c / midi/mm.c mm_load)."""
    rows = [("mmch", mmch)]
    for act, key in zip(ACTIONS, ("mm0", "mm1", "mm2")):
        rows.append((key, ccs[act]))
    return rows


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("check", "template", "test"):
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "template":
        print(json.dumps({"version": 1, "channel": 1,
                          "mappings": [{"action": a, "cc": DEFAULTS[a]} for a in ACTIONS]}, indent=2))
        return
    if cmd == "check":
        if len(sys.argv) != 3:
            sys.exit("usage: op1midi_map.py check <mapping.json>")
        try:
            mmch, ccs = load(sys.argv[2])
        except Bad as e:
            sys.exit(f"INVALID: {e}")
        print("valid.")
        for k, v in device_values(mmch, ccs):
            print(f"  {k} = {v}")
        return
    # test: send the trigger CCs of a mapping file (or the defaults) over USB MIDI
    import argparse
    try:
        import mido
    except ImportError:
        sys.exit("need: pip install mido python-rtmidi")
    ap = argparse.ArgumentParser()
    ap.add_argument("mapping", nargs="?", help="mapping.json (defaults without one)")
    ap.add_argument("--ch", type=int, default=1, help="MIDI channel 1-16")
    ap.add_argument("--port", default="OP-1")
    ap.add_argument("--cc", type=int, help="only this CC")
    a = ap.parse_args()
    if a.mapping:
        try:
            mmch, ccs = load(a.mapping)
        except Bad as e:
            sys.exit(f"INVALID: {e}")
        if mmch == 17:
            pass
        elif mmch != a.ch:
            sys.exit(f"note: the mapping listens on channel {mmch}, sending on {a.ch}")
    else:
        ccs = dict(DEFAULTS)
    name = next((p for p in mido.get_output_names() if a.port in p), None) or sys.exit(f'no port with "{a.port}"')
    out = mido.open_output(name)

    def trigger(cc):
        for v in (127, 0):
            out.send(mido.Message("control_change", channel=a.ch - 1, control=cc, value=v))
            import time; time.sleep(0.03)

    if a.cc is not None:
        trigger(a.cc)
        return
    for act in ACTIONS:
        cc = ccs[act]
        if not cc:
            print(f"{act}: unassigned, skipped")
            continue
        input(f"Enter -> {act} (CC {cc}) ")
        trigger(cc)
    print("done")


if __name__ == "__main__":
    main()
