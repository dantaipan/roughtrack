#!/usr/bin/env python3
"""Cuts RoughTrack's grooves.xml from the Groove MIDI Dataset.

    python3 tools/build_grooves.py <groove folder holding info.csv> <grooves.xml>

Tick 0 in these files is the drummer's first hit, not bar 1, so each file's bar
phase is found from its backbeat before anything is cut. Only 4/4 sections that
hold steady for their whole length are kept, so a groove can loop.
"""

import collections
import csv
import math
import statistics
import struct
import sys
import xml.etree.ElementTree as ElementTree

KICK, SNARE, SIDE_STICK, HAT_CLOSED, HAT_OPEN, HAT_PEDAL, TOM_HIGH, TOM_MID, TOM_LOW, CRASH, CRASH_2, RIDE, RIDE_BELL = range(13)

# Roland TD-11 notes, as recorded in the dataset.
PIECES = {36: KICK, 38: SNARE, 40: SNARE, 37: SIDE_STICK, 42: HAT_CLOSED, 22: HAT_CLOSED, 46: HAT_OPEN, 26: HAT_OPEN,
          44: HAT_PEDAL, 48: TOM_HIGH, 50: TOM_HIGH, 45: TOM_MID, 47: TOM_MID, 43: TOM_LOW, 58: TOM_LOW,
          49: CRASH, 55: CRASH, 57: CRASH_2, 52: CRASH_2, 51: RIDE, 59: RIDE, 53: RIDE_BELL}

TOMS = {TOM_HIGH, TOM_MID, TOM_LOW}
BEATS_PER_BAR = 4
GROOVE_BARS = 8
EARLY_BEATS = 0.125  # Must match grooveEarlyBeats in the engine.
PHASE_BINS = 384
GROOVES_PER_STYLE = 24
MINIMUM_STYLE_GROOVES = 3

# The dataset has one long blues take and a few short ones, so Blues also draws on
# shuffles recorded under other styles, cut again here under their own ids.
BLUES_STYLE = "Blues"
BLUES_SHORT_BARS = 4
WINDOWS_PER_BLUES_TAKE = 4
BLUES_GROOVES = 24
BLUES_SLACK = 2

STYLE_NAMES = {"rock": "Rock", "funk": "Funk", "pop": "Pop", "soul": "Soul", "hiphop": "Hip-Hop", "blues": "Blues",
               "punk": "Punk", "country": "Country", "reggae": "Reggae", "jazz": "Jazz", "latin": "Latin",
               "afrobeat": "Afrobeat", "neworleans": "New Orleans", "dance": "Dance", "gospel": "Gospel"}


def read_variable_length(data, index):
    value = 0

    while True:
        byte = data[index]
        index += 1
        value = value << 7 | byte & 127

        if byte < 128:
            return value, index


def read_hits(path):
    """Returns (beat, piece, velocity) for each drum hit, in time order."""
    data = open(path, "rb").read()
    ticks_per_beat = struct.unpack(">H", data[12:14])[0]
    index = 14
    hits = []

    while index < len(data):
        length = struct.unpack(">I", data[index + 4:index + 8])[0]
        track = data[index + 8:index + 8 + length]
        index += 8 + length
        position = tick = status = 0

        while position < len(track):
            delta, position = read_variable_length(track, position)
            tick += delta

            if track[position] >= 128:
                status = track[position]
                position += 1

            if status == 255:
                size, after = read_variable_length(track, position + 1)
                position = after + size
            elif status in (240, 247):
                size, position = read_variable_length(track, position)
                position += size
            elif status & 0xF0 in (0xC0, 0xD0):
                position += 1
            else:
                if status & 0xF0 == 0x90 and track[position + 1] > 0 and track[position] in PIECES:
                    hits.append((tick / ticks_per_beat, PIECES[track[position]], track[position + 1]))

                position += 2

    return sorted(hits)


def find_bar_phase(hits):
    """The offset that best puts snares on 2 and 4, kicks on 1 and everything else on the grid."""
    period = BEATS_PER_BAR
    bins = PHASE_BINS
    snare = [0.0] * bins
    kick = [0.0] * bins

    for beat, piece, velocity in hits:
        slot = int((beat % period) / period * bins) % bins

        if piece == SNARE:
            snare[slot] += velocity
        elif piece == KICK:
            kick[slot] += velocity

    width = 3
    best, best_score = 0, -1.0

    for shift in range(bins):
        score = 0.0

        for offset in range(-width, width + 1):
            at = lambda beats: (shift + int(beats / period * bins) + offset) % bins
            score += snare[at(1)] + snare[at(3)] + 0.5 * kick[at(0)] + 0.25 * kick[at(2)]

        if score > best_score:
            best, best_score = shift, score

    return best / bins * period


def sixteenth(beat):
    return round(beat * 4) % (BEATS_PER_BAR * 4)


def score_window(bars, slack=0):
    """How steady and playable a run of bars is, or None if it won't loop. Slack
    is how many more bars may stray, for playing looser than a steady rock beat."""
    patterns = []
    backbeats = 0
    downbeats = 0
    toms = 0

    for bar in bars:
        kicks = {sixteenth(beat) for beat, piece, _ in bar if piece == KICK}
        snares = {sixteenth(beat) for beat, piece, velocity in bar if piece == SNARE and velocity > 45}

        if len(bar) < 6 or not kicks:
            return None

        backbeats += 4 in snares and 12 in snares
        downbeats += 0 in kicks
        toms += sum(piece in TOMS for _, piece, _ in bar)
        patterns.append(kicks | {16 + slot for slot in snares})

    if backbeats < len(bars) - 1 - slack or downbeats < len(bars) - 2 - slack or toms > 6:
        return None

    pairs = [(patterns[i], patterns[j]) for i in range(len(bars)) for j in range(i + 1, len(bars))]
    return statistics.mean(len(a & b) / len(a | b) for a, b in pairs)


def describe(hits):
    counts = collections.Counter(piece for _, piece, _ in hits)
    cymbal = "ride" if counts[RIDE] + counts[RIDE_BELL] > counts[HAT_CLOSED] + counts[HAT_OPEN] else "hi-hat"

    offbeats = [beat % 1 for beat, piece, _ in hits if piece in (HAT_CLOSED, RIDE, HAT_OPEN) and 0.4 < beat % 1 < 0.75]
    shuffled = len(offbeats) > 8 and statistics.median(offbeats) > 0.59

    return cymbal + (", shuffle" if shuffled else "")


def encode(hits):
    return ";".join(f"{beat:.4f},{piece},{velocity}" for beat, piece, velocity in hits)


def is_swung_fill(hits):
    """True if the notes between the beats fall on triplets rather than sixteenths."""
    tolerance = 0.06
    fractions = [beat % 1 for beat, _, _ in hits]
    near = lambda positions: sum(any(abs(fraction - position) < tolerance for position in positions) for fraction in fractions)
    triplets = near((1 / 3, 2 / 3))

    return triplets >= 4 and triplets >= 2 * near((0.25, 0.5, 0.75))


def split_into_bars(hits):
    phase = find_bar_phase(hits)
    bars = collections.defaultdict(list)

    for beat, piece, velocity in hits:
        bar = math.floor((beat - phase + EARLY_BEATS) / BEATS_PER_BAR)
        bars[bar].append((beat - phase - bar * BEATS_PER_BAR, piece, velocity))

    return bars


def join_bars(bars, start, count):
    return [(beat + i * BEATS_PER_BAR, piece, velocity) for i in range(count) for beat, piece, velocity in bars[start + i]]


def find_windows(bars, length, count, first_bar, slack=0):
    """The steadiest runs of bars that don't overlap, best first."""
    scored = []

    for start in range(first_bar, max(bars) - length + 2):
        score = score_window([bars[start + i] for i in range(length)], slack)

        if score is not None:
            scored.append((score, start))

    chosen = []

    for score, start in sorted(scored, reverse=True):
        if len(chosen) < count and all(abs(start - other) >= length for _, other in chosen):
            chosen.append((score, start))

    return chosen


def cut_blues(rows, folder):
    grooves = []

    for row in rows:
        if row["beat_type"] != "beat" or row["time_signature"] != "4-4":
            continue

        tagged = row["style"].startswith("blues")
        hits = read_hits(folder + "/" + row["midi_filename"])

        if len(hits) < 16:
            continue

        bars = split_into_bars(hits)

        if tagged and max(bars) < GROOVE_BARS:
            windows, length = find_windows(bars, BLUES_SHORT_BARS, 1, 0, BLUES_SLACK), BLUES_SHORT_BARS
        elif tagged:
            windows, length = find_windows(bars, GROOVE_BARS, WINDOWS_PER_BLUES_TAKE, 1, BLUES_SLACK), GROOVE_BARS
        elif "shuffle" in row["style"] or "shuffle" in describe(hits):
            windows, length = find_windows(bars, GROOVE_BARS, 1, 1), GROOVE_BARS
        else:
            continue

        for score, start in windows:
            window = join_bars(bars, start, length)

            if "shuffle" in row["style"] or "shuffle" in describe(window):
                grooves.append({"id": row["id"].replace("/", "-") + f"-bar{start}-blues", "bpm": int(row["bpm"]), "bars": length,
                                "hits": window, "score": score + (1.0 if tagged else 0.0)})

    return sorted(grooves, key=lambda groove: -groove["score"])[:BLUES_GROOVES]


def cut_grooves(rows, folder):
    found = collections.defaultdict(list)

    for row in rows:
        if row["beat_type"] != "beat" or row["time_signature"] != "4-4":
            continue

        hits = read_hits(folder + "/" + row["midi_filename"])

        if len(hits) < 40:
            continue

        phase = find_bar_phase(hits)
        hits = [(beat - phase, piece, velocity) for beat, piece, velocity in hits]
        last_bar = math.floor(hits[-1][0] / BEATS_PER_BAR)

        bars = collections.defaultdict(list)

        for beat, piece, velocity in hits:
            bar = math.floor((beat + EARLY_BEATS) / BEATS_PER_BAR)
            bars[bar].append((beat - bar * BEATS_PER_BAR, piece, velocity))

        best, best_score = None, 0.0

        for start in range(1, last_bar - GROOVE_BARS + 1):
            score = score_window([bars[start + i] for i in range(GROOVE_BARS)])

            if score is not None and score > best_score:
                best, best_score = start, score

        if best is None:
            continue

        window = [(beat + i * BEATS_PER_BAR, piece, velocity)
                  for i in range(GROOVE_BARS) for beat, piece, velocity in bars[best + i]]

        style = row["style"].split("/")[0]
        found[style].append({"id": row["id"].replace("/", "-") + f"-bar{best}", "bpm": int(row["bpm"]),
                             "hits": window, "score": best_score})

    return found


def cut_fills(rows, folder):
    found = collections.defaultdict(list)

    for row in rows:
        if row["beat_type"] != "fill" or row["time_signature"] != "4-4":
            continue

        hits = read_hits(folder + "/" + row["midi_filename"])

        if len(hits) < 8:
            continue

        start = hits[0][0]
        hits = [(beat - start, piece, velocity) for beat, piece, velocity in hits]
        length = hits[-1][0]

        # Only a fill that fills its bars can stand in for a bar of the beat.
        if 3.25 <= length < 3.9:
            bars = 1
        elif 7.25 <= length < 7.9:
            bars = 2
        else:
            continue

        style = row["style"].split("/")[0]
        fill = {"id": row["id"].replace("/", "-") + "-fill", "bpm": int(row["bpm"]), "hits": hits, "bars": bars}
        found[style].append(fill)

        if is_swung_fill(hits):
            found[BLUES_STYLE].append({**fill, "id": fill["id"] + "-blues"})

    return found


def main():
    folder, output = sys.argv[1], sys.argv[2]
    rows = list(csv.DictReader(open(folder + "/info.csv")))

    grooves = cut_grooves(rows, folder)
    fills = cut_fills(rows, folder)
    root = ElementTree.Element("GROOVES", {"source": "Groove MIDI Dataset v1.0.0, CC BY 4.0"})
    names_used = collections.Counter()

    def unique_name(style, name):
        names_used[style, name] += 1
        count = names_used[style, name]
        return name if count == 1 else f"{name} ({count})"

    def add_groove(style, groove):
        bars = groove.get("bars", GROOVE_BARS)
        ElementTree.SubElement(root, "GROOVE", {"id": groove["id"], "style": style, "kind": "beat", "bars": str(bars),
                                                "bpm": str(groove["bpm"]),
                                                "name": unique_name(style, f"{groove['bpm']} BPM, {describe(groove['hits'])}"),
                                                "hits": encode(groove["hits"])})

    def add_fill(style, fill):
        ElementTree.SubElement(root, "GROOVE", {"id": fill["id"], "style": style, "kind": "fill", "bars": str(fill["bars"]),
                                                "bpm": str(fill["bpm"]),
                                                "name": unique_name(style, f"{fill['bpm']} BPM, {fill['bars']} bar{'s' if fill['bars'] > 1 else ''}"),
                                                "hits": encode(fill["hits"])})

    blues = sorted(cut_blues(rows, folder), key=lambda groove: groove["bpm"])
    blues_fills = sorted(fills[BLUES_STYLE], key=lambda fill: fill["bpm"])

    for groove in blues:
        add_groove(BLUES_STYLE, groove)

    for fill in blues_fills:
        add_fill(BLUES_STYLE, fill)

    print(f"{BLUES_STYLE}: {len(blues)} grooves, {len(blues_fills)} fills")

    for style in sorted(grooves, key=lambda name: -len(grooves[name])):
        if style == "blues" or len(grooves[style]) < MINIMUM_STYLE_GROOVES or style not in STYLE_NAMES:
            print(f"skipped {style}: {len(grooves[style])}")
            continue

        name = STYLE_NAMES[style]
        best = sorted(grooves[style], key=lambda groove: -groove["score"])[:GROOVES_PER_STYLE]

        for groove in sorted(best, key=lambda groove: groove["bpm"]):
            add_groove(name, groove)

        for fill in sorted(fills[style], key=lambda fill: fill["bpm"]):
            add_fill(name, fill)

        print(f"{name}: {len(best)} grooves (from {len(grooves[style])}), {len(fills[style])} fills")

    ElementTree.ElementTree(root).write(output, encoding="UTF-8", xml_declaration=True)


if __name__ == "__main__":
    main()
