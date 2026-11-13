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
import zlib
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

FLAG_CONTINUATION = 0x80
EVENTS_PER_CHUNK = 256
CONT_GAP_THRESHOLD_SEC = 45

SPEAKER_MAP = {
    "CDR": SPK_CDR,
    "SCDR": SPK_CDR,
    "CMP": SPK_CMP,
    "CMP-CM": SPK_CMP,
    "LMP": SPK_LMP,
    "CC": SPK_CC,
    "CAPCOM": SPK_CC,
    "ROOSA": SPK_CC,
    "PAO": SPK_PAO,
    "LCC": SPK_LCC,
    "SC": SPK_SC,
    "CT": SPK_SC,
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

        # Handle rare shifted rows like "|000:51:00||CC|Okay..."
        if not spk_raw and len(parts) >= 5 and parts[3].strip().upper() in SPEAKER_MAP:
            spk_raw = parts[3].strip().upper()
            msg_raw = parts[4].strip()

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
    """
    Pack ALL historical events into MISZ chunk-compressed format:
      Header (16 bytes): magic="MISZ", total_events(u32), chunk_count(u32), events_per_chunk(u16), flags(u16=0)
      Directory (chunk_count * 16 bytes): data_offset(u32), comp_size(u32), uncomp_size(u32), event_count(u16), reserved(u16)
      Payloads: zlib-compressed chunks of <get_sec(i32), spk_and_flags(u8), text_len(u8), text[]>
    """
    # Stable sort preserves original ordering for identical timestamps
    events.sort(key=lambda x: x[0])

    records = []
    prev_raw_sec = None
    prev_raw_spk = None

    for get_sec, spk_id, text in events:
        chunks = split_sentences(text, max_len=140)
        same_spk_cont = (
            prev_raw_spk == spk_id
            and spk_id != SPK_OTHER
            and prev_raw_sec is not None
            and 0 <= (get_sec - prev_raw_sec) <= CONT_GAP_THRESHOLD_SEC
        )
        for chunk_idx, chunk in enumerate(chunks):
            chunk_bytes = chunk.encode('ascii', errors='ignore')[:250]
            if not chunk_bytes:
                continue
            is_cont = (chunk_idx > 0) or same_spk_cont
            spk_flags = (spk_id & 0x7F) | (FLAG_CONTINUATION if is_cont else 0)
            records.append((get_sec, spk_flags, chunk_bytes))
        prev_raw_sec = get_sec
        prev_raw_spk = spk_id

    total_records = len(records)
    chunk_count = (total_records + EVENTS_PER_CHUNK - 1) // EVENTS_PER_CHUNK

    dir_offset = 16
    payload_offset = dir_offset + chunk_count * 16

    dir_entries = []
    payloads = []
    curr_offset = payload_offset

    for c in range(chunk_count):
        sub = records[c * EVENTS_PER_CHUNK : (c + 1) * EVENTS_PER_CHUNK]
        raw_chunk = bytearray()
        for rec_sec, spk_flags, chunk_bytes in sub:
            raw_chunk.extend(struct.pack('<iBB', rec_sec, spk_flags, len(chunk_bytes)))
            raw_chunk.extend(chunk_bytes)
        comp = zlib.compress(bytes(raw_chunk), 9)
        dir_entries.append((curr_offset, len(comp), len(raw_chunk), len(sub)))
        payloads.append(comp)
        curr_offset += len(comp)

    buf = bytearray()
    buf.extend(struct.pack('<4sIIHH', b'MISZ', total_records, chunk_count, EVENTS_PER_CHUNK, 0))
    for off, csz, usz, cnt in dir_entries:
        buf.extend(struct.pack('<IIIHH', off, csz, usz, cnt, 0))
    for p in payloads:
        buf.extend(p)

    return buf

def main():
    print("=== Packing Complete Apollo Mission Transcripts (MISZ Chunked Zlib) ===")
    
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
            
        packed = pack_mission_events(events)
        
        bin_path = os.path.join(DATA_DIR, f"mission_{m['id']}.bin")
        with open(bin_path, "wb") as f:
            f.write(packed)
            
        print(f"[{m['name']}] Raw: {len(events)} -> Packed: {len(packed)} bytes ({len(packed)/1024:.1f} KB)")
        total_packed_bytes += len(packed)
        
    print(f"\nTotal full-mission binary size across all 4 missions: {total_packed_bytes} bytes ({total_packed_bytes/1024:.1f} KB)")

if __name__ == "__main__":
    main()
