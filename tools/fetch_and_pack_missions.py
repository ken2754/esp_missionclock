#!/usr/bin/env python3
"""
tools/fetch_and_pack_missions.py
Download and pack curated NASA Apollo 8, 11, 13, and 17 transcripts into compact binary formats.
"""

import os
import sys
import re
import struct
import urllib.request

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(BASE_DIR, "main", "data")
MAIN_DIR = os.path.join(BASE_DIR, "main")

os.makedirs(DATA_DIR, exist_ok=True)

# Speaker IDs
SPK_OTHER = 0
SPK_CDR   = 1  # Commander
SPK_CMP   = 2  # Command Module Pilot
SPK_LMP   = 3  # Lunar Module Pilot
SPK_CC    = 4  # Capsule Communicator (CAPCOM)
SPK_PAO   = 5  # Public Affairs Officer
SPK_LCC   = 6  # Launch Control Center
SPK_SC    = 7  # Spacecraft (Crew general)

SPEAKER_MAP = {
    "CDR": SPK_CDR,
    "CMP": SPK_CMP,
    "LMP": SPK_LMP,
    "CC": SPK_CC,
    "CAPCOM": SPK_CC,
    "PAO": SPK_PAO,
    "LCC": SPK_LCC,
    "SC": SPK_SC,
}

def clean_ascii(text):
    text = text.replace("’", "'").replace("‘", "'").replace("“", '"').replace("”", '"')
    text = text.replace("—", "--").replace("–", "-").replace("…", "...")
    # Remove glossary tags like [glossary:P11] -> P11
    text = re.sub(r'\[glossary:([^\]]+)\]', r'\1', text)
    # Remove footnotes or markdown links [text](url) -> text
    text = re.sub(r'\[([^\]]+)\]\([^\)]+\)', r'\1', text)
    # Normalize whitespaces
    text = re.sub(r'\s+', ' ', text).strip()
    # Filter to printable ASCII
    return "".join(c for c in text if 32 <= ord(c) <= 126)

def parse_spacelog_time(ts_str):
    ts_str = ts_str.strip("[]")
    is_neg = False
    if ts_str.startswith("-"):
        is_neg = True
        ts_str = ts_str[1:]
    parts = ts_str.split(":")
    if len(parts) == 4:
        d, h, m, s = map(int, parts)
        sec = d * 86400 + h * 3600 + m * 60 + s
    elif len(parts) == 3:
        h, m, s = map(int, parts)
        sec = h * 3600 + m * 60 + s
    else:
        sec = int(parts[0])
    return -sec if is_neg else sec

def parse_spacelog_transcript(raw_text):
    events = []
    pattern = re.compile(r'\[(-?\d+(?::\d+){2,3})\]\s*([^\[]+)')
    for m in pattern.finditer(raw_text):
        ts_raw = m.group(1)
        body = m.group(2)
        get_sec = parse_spacelog_time(ts_raw)
        
        lines = [line.strip() for line in body.splitlines() if line.strip() and not line.strip().startswith("_")]
        if not lines:
            continue
        full_text = " ".join(lines)
        
        spk_id = SPK_OTHER
        m_spk = re.match(r'^([A-Z0-9]+)(?:\s*\([^\)]+\))?:\s*(.*)', full_text)
        if m_spk:
            spk_name = m_spk.group(1).upper()
            msg = m_spk.group(2)
            spk_id = SPEAKER_MAP.get(spk_name, SPK_OTHER)
        else:
            msg = full_text
            
        clean_msg = clean_ascii(msg)
        if clean_msg:
            events.append((get_sec, spk_id, clean_msg))
    return events

def parse_apollo17_csv(raw_text):
    events = []
    for line in raw_text.splitlines():
        line = line.strip()
        if not line or not line.startswith("|"):
            continue
        parts = line.split("|")
        if len(parts) < 4:
            continue
        ts_raw = parts[1].strip()
        spk_raw = parts[2].strip().upper()
        msg_raw = parts[3].strip()
        
        if not ts_raw:
            continue
        
        is_neg = False
        if ts_raw.startswith("-"):
            is_neg = True
            ts_raw = ts_raw[1:]
        t_parts = ts_raw.split(":")
        try:
            if len(t_parts) == 4:
                d, h, m, s = map(int, t_parts)
                sec = d * 86400 + h * 3600 + m * 60 + s
            elif len(t_parts) == 3:
                h, m, s = map(int, t_parts)
                sec = h * 3600 + m * 60 + s
            else:
                continue
        except ValueError:
            continue
            
        get_sec = -sec if is_neg else sec
        spk_id = SPEAKER_MAP.get(spk_raw, SPK_OTHER)
        clean_msg = clean_ascii(msg_raw)
        if clean_msg:
            events.append((get_sec, spk_id, clean_msg))
    return events

def pack_mission_events(events):
    # Sort events by get_sec
    events.sort(key=lambda x: x[0])
    buf = bytearray()
    # Write header:
    # uint32_t magic = 0x4D495353 ('MISS')
    # uint32_t event_count (dummy, updated after)
    buf.extend(b'MISS')
    buf.extend(struct.pack('<I', 0))
    
    total_records = 0
    for get_sec, spk_id, text in events:
        chunks = [text[i:i+230] for i in range(0, len(text), 230)]
        for chunk in chunks:
            chunk_bytes = chunk.encode('ascii', errors='ignore')
            buf.extend(struct.pack('<iBB', get_sec, spk_id, len(chunk_bytes)))
            buf.extend(chunk_bytes)
            total_records += 1
            
    # Update count
    struct.pack_into('<I', buf, 4, total_records)
    return buf

def filter_mission_events(m_id, events):
    """Filter events to curated highlights ensuring full continuity during key phases."""
    if m_id == "a17":
        # Apollo 17: User requested pre-launch is the main star!
        # Pre-launch hold (-9442s to 0s) + Launch & early orbit (up to 12h = 43200s)
        # plus Lunar final EVA closing speech (around GET 170h)
        filtered = []
        for e in events:
            sec = e[0]
            # All pre-launch events (-9000 to 0)
            if sec < 0:
                filtered.append(e)
            # Liftoff and first 10 hours
            elif sec <= 3600 * 10:
                filtered.append(e)
            # Final EVA closing speech (Gene Cernan's final footsteps on the Moon)
            elif 612000 <= sec <= 618000:
                filtered.append(e)
        return filtered
    elif m_id == "a11":
        # Apollo 11: Liftoff (0 to 4h) + Lunar Descent/Landing (102h to 104h) + Lunar EVA (109h to 112h)
        filtered = []
        for e in events:
            sec = e[0]
            if sec <= 3600 * 4: # Liftoff & Earth orbit
                filtered.append(e)
            elif 3600 * 102 <= sec <= 3600 * 104: # Landing & "The Eagle has landed"
                filtered.append(e)
            elif 3600 * 109 <= sec <= 3600 * 112: # EVA & "That's one small step"
                filtered.append(e)
        return filtered
    elif m_id == "a13":
        # Apollo 13: Liftoff (0 to 3h) + Crisis ("Houston we've had a problem", 55h to 60h) + Reentry (140h to 143h)
        filtered = []
        for e in events:
            sec = e[0]
            if sec <= 3600 * 3:
                filtered.append(e)
            elif 3600 * 55 <= sec <= 3600 * 60: # Crisis & lifeboat activation
                filtered.append(e)
            elif 3600 * 140 <= sec <= 3600 * 143: # Reentry & recovery
                filtered.append(e)
        return filtered
    elif m_id == "a08":
        # Apollo 8: Liftoff (0 to 3h) + Lunar Orbit & Genesis reading (85h to 88h) + Return (89h to 90h)
        filtered = []
        for e in events:
            sec = e[0]
            if sec <= 3600 * 3:
                filtered.append(e)
            elif 3600 * 85 <= sec <= 3600 * 88: # Lunar orbit, Earthrise & Genesis reading
                filtered.append(e)
            elif 3600 * 89 <= sec <= 3600 * 90: # "There is a Santa Claus"
                filtered.append(e)
        return filtered
    return events

def main():
    print("=== Curating and Packing Apollo Mission Transcripts ===")
    
    missions = [
        {"id": "a08", "name": "Apollo 8", "source_type": "spacelog", "cache": "data/a08_raw.txt"},
        {"id": "a11", "name": "Apollo 11", "source_type": "spacelog", "cache": "data/a11_raw.txt"},
        {"id": "a13", "name": "Apollo 13", "source_type": "spacelog", "cache": "data/a13_raw.txt"},
        {"id": "a17", "name": "Apollo 17", "source_type": "apollo17_csv", "cache": "data/a17_raw.txt"},
    ]
    
    total_packed_bytes = 0
    
    for m in missions:
        cache_path = os.path.join(BASE_DIR, m["cache"])
        with open(cache_path, "r", encoding="utf-8", errors="ignore") as f:
            raw_text = f.read()
            
        if m["source_type"] == "spacelog":
            events = parse_spacelog_transcript(raw_text)
        else:
            events = parse_apollo17_csv(raw_text)
            
        curated = filter_mission_events(m["id"], events)
        packed = pack_mission_events(curated)
        
        bin_path = os.path.join(DATA_DIR, f"mission_{m['id']}.bin")
        with open(bin_path, "wb") as f:
            f.write(packed)
            
        print(f"[{m['name']}] Raw: {len(events)} -> Curated: {len(curated)} events -> Packed: {len(packed)} bytes ({len(packed)/1024:.1f} KB)")
        total_packed_bytes += len(packed)
        
    print(f"\nTotal curated binary size across all 4 missions: {total_packed_bytes} bytes ({total_packed_bytes/1024:.1f} KB)")

if __name__ == "__main__":
    main()
