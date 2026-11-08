#!/usr/bin/env python3
"""
tools/fetch_and_pack_missions.py
Download and pack curated NASA Apollo 8, 11, 13, and 17 transcripts into compact binary formats.
Properly splits long utterances on sentence/word boundaries so no words are cut in half,
and spaces out multi-sentence announcements by a few seconds to prevent queue flooding.
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
    text = re.sub(r'\[glossary:([^\]]+)\]', r'\1', text)
    text = re.sub(r'\[([^\]]+)\]\([^\)]+\)', r'\1', text)
    text = re.sub(r'\s+', ' ', text).strip()
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

def split_sentences(text, max_len=140):
    """
    Split long text into sentence or word-boundary chunks (up to max_len chars).
    Never cuts a word in half.
    """
    if len(text) <= max_len:
        return [text]
        
    chunks = []
    # Try splitting by sentence first (ending in . ! ?)
    sentences = re.split(r'(?<=[.!?])\s+', text)
    current_chunk = ""
    
    for s in sentences:
        s = s.strip()
        if not s:
            continue
        if len(s) > max_len:
            # Sentence itself is longer than max_len: split by words
            words = s.split(' ')
            sub_chunk = ""
            for w in words:
                if len(sub_chunk) + len(w) + 1 <= max_len:
                    sub_chunk = f"{sub_chunk} {w}".strip()
                else:
                    if sub_chunk:
                        chunks.append(sub_chunk)
                    sub_chunk = w
            if sub_chunk:
                chunks.append(sub_chunk)
        else:
            if len(current_chunk) + len(s) + 1 <= max_len:
                current_chunk = f"{current_chunk} {s}".strip()
            else:
                if current_chunk:
                    chunks.append(current_chunk)
                current_chunk = s
    if current_chunk:
        chunks.append(current_chunk)
        
    return chunks

def pack_mission_events(events):
    # Sort events by get_sec
    events.sort(key=lambda x: x[0])
    buf = bytearray()
    buf.extend(b'MISS')
    buf.extend(struct.pack('<I', 0))
    
    total_records = 0
    for get_sec, spk_id, text in events:
        chunks = split_sentences(text, max_len=140)
        time_offset = 0
        for chunk in chunks:
            chunk_bytes = chunk.encode('ascii', errors='ignore')
            if len(chunk_bytes) > 250:
                chunk_bytes = chunk_bytes[:250]
            # Advance time by 4 seconds for subsequent sentences of a long announcement
            rec_sec = get_sec + time_offset
            buf.extend(struct.pack('<iBB', rec_sec, spk_id, len(chunk_bytes)))
            buf.extend(chunk_bytes)
            total_records += 1
            time_offset += 4
            
    struct.pack_into('<I', buf, 4, total_records)
    return buf

def filter_mission_events(m_id, events):
    if m_id == "a17":
        filtered = []
        for e in events:
            sec = e[0]
            if sec < 0:
                filtered.append(e)
            elif sec <= 3600 * 10:
                filtered.append(e)
            elif 612000 <= sec <= 618000:
                filtered.append(e)
        return filtered
    elif m_id == "a11":
        filtered = []
        for e in events:
            sec = e[0]
            if sec <= 3600 * 4:
                filtered.append(e)
            elif 3600 * 102 <= sec <= 3600 * 104:
                filtered.append(e)
            elif 3600 * 109 <= sec <= 3600 * 112:
                filtered.append(e)
        return filtered
    elif m_id == "a13":
        filtered = []
        for e in events:
            sec = e[0]
            if sec <= 3600 * 3:
                filtered.append(e)
            elif 3600 * 55 <= sec <= 3600 * 60:
                filtered.append(e)
            elif 3600 * 140 <= sec <= 3600 * 143:
                filtered.append(e)
        return filtered
    elif m_id == "a08":
        filtered = []
        for e in events:
            sec = e[0]
            if sec <= 3600 * 3:
                filtered.append(e)
            elif 3600 * 85 <= sec <= 3600 * 88:
                filtered.append(e)
            elif 3600 * 89 <= sec <= 3600 * 90:
                filtered.append(e)
        return filtered
    return events

def main():
    print("=== Curating and Packing Apollo Mission Transcripts (Clean Boundary Split) ===")
    
    missions = [
        {"id": "a08", "name": "Apollo 8", "source_type": "spacelog", "url": "https://raw.githubusercontent.com/Spacelog/Spacelog/master/missions/a8/transcripts/TEC"},
        {"id": "a11", "name": "Apollo 11", "source_type": "spacelog", "url": "https://raw.githubusercontent.com/Spacelog/Spacelog/master/missions/a11/transcripts/TEC"},
        {"id": "a13", "name": "Apollo 13", "source_type": "spacelog", "url": "https://raw.githubusercontent.com/Spacelog/Spacelog/master/missions/a13/transcripts/TEC"},
        {"id": "a17", "name": "Apollo 17", "source_type": "apollo17_csv", "url": "https://raw.githubusercontent.com/bfeist/Apollo17/master/MISSION_DATA/A17%20master%20TEC%20and%20PAO%20utterances.csv"},
    ]
    
    total_packed_bytes = 0
    
    for m in missions:
        cache_path = os.path.join(DATA_DIR, f"{m['id']}_raw.txt")
        if not os.path.exists(cache_path):
            print(f"Downloading {m['name']} from {m['url']} ...")
            req = urllib.request.Request(m['url'], headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req) as resp, open(cache_path, 'wb') as f:
                f.write(resp.read())
            print(f"  Saved to {cache_path}")

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
            
        print(f"[{m['name']}] Raw: {len(events)} -> Curated: {len(curated)} -> Packed: {len(packed)} bytes ({len(packed)/1024:.1f} KB)")
        total_packed_bytes += len(packed)
        
    print(f"\nTotal curated binary size across all 4 missions: {total_packed_bytes} bytes ({total_packed_bytes/1024:.1f} KB)")

if __name__ == "__main__":
    main()
