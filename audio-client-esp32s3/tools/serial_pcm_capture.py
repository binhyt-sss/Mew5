#!/usr/bin/env python3
import argparse
import base64
import re
import time
import wave

import serial


def main() -> None:
    parser = argparse.ArgumentParser(description="Capture PCM test stream from ESP32 serial and save WAV")
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=int, default=30, help="seconds to wait for PCM_END")
    parser.add_argument("--output", default="serial_test.wav")
    parser.add_argument("--reset", action="store_true", help="toggle DTR/RTS to reset ESP32 before capture")
    parser.add_argument("--reset-delay", type=float, default=0.25, help="seconds to wait after reset pulse")
    args = parser.parse_args()

    begin_re = re.compile(r"PCM_BEGIN\s+sample_rate=(\d+)\s+channels=(\d+)\s+bits_per_sample=(\d+)\s+duration_s=(\d+)")
    chunk_re = re.compile(r"PCM_CHUNK:(\d+):(.+)$")

    sample_rate = 16000
    channels = 1
    bits_per_sample = 16
    pcm = bytearray()

    started = time.time()
    got_begin = False

    if args.reset:
        try:
            with serial.Serial(args.port, args.baud, timeout=0.2) as pre:
                pre.dtr = False
                pre.rts = True
                time.sleep(0.12)
                pre.dtr = True
                pre.rts = False
            time.sleep(max(args.reset_delay, 0.0))
            print("[INFO] Reset pulse sent")
        except Exception as exc:
            print(f"[WARN] Reset failed: {exc}")

    with serial.Serial(args.port, args.baud, timeout=0.5) as ser:
        print(f"[INFO] Listening on {args.port} @ {args.baud}")
        while True:
            if time.time() - started > args.timeout:
                print("[WARN] Timeout reached")
                break

            line = ser.readline()
            if not line:
                continue

            text = line.decode("utf-8", errors="ignore").strip()
            if not text:
                continue

            m_begin = begin_re.search(text)
            if m_begin:
                sample_rate = int(m_begin.group(1))
                channels = int(m_begin.group(2))
                bits_per_sample = int(m_begin.group(3))
                got_begin = True
                print(f"[INFO] PCM_BEGIN sr={sample_rate}, ch={channels}, bps={bits_per_sample}")
                continue

            m_chunk = chunk_re.search(text)
            if m_chunk:
                if not got_begin:
                    continue
                b64 = m_chunk.group(2)
                try:
                    chunk = base64.b64decode(b64)
                except Exception:
                    continue
                pcm.extend(chunk)
                if (len(pcm) // 2) % 16000 == 0:
                    print(f"[INFO] Captured {len(pcm)} bytes")
                continue

            if "PCM_END" in text:
                print(text)
                break

    if not pcm:
        raise SystemExit("[ERROR] No PCM data captured")

    sampwidth = max(bits_per_sample // 8, 1)
    with wave.open(args.output, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(sampwidth)
        wf.setframerate(sample_rate)
        wf.writeframes(bytes(pcm))

    print(f"[OK] Saved WAV: {args.output} ({len(pcm)} bytes PCM)")


if __name__ == "__main__":
    main()
