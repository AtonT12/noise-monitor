#!/usr/bin/env python3
"""Generate a small 16 kHz, 16-bit mono WAV file for the speaker test."""

from __future__ import annotations

import math
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "data" / "alert.wav"
SAMPLE_RATE = 16_000
AMPLITUDE = 5_000  # Intentionally modest; adjust only after checking speaker level.
NOTES = [(784.0, 0.22), (0.0, 0.08), (659.0, 0.22), (0.0, 0.08), (523.0, 0.55)]


def make_samples() -> list[int]:
    result: list[int] = []
    for frequency, duration in NOTES:
        count = int(SAMPLE_RATE * duration)
        phase = 0.0
        for index in range(count):
            if frequency == 0.0:
                result.append(0)
                continue
            t = index / SAMPLE_RATE
            attack = min(1.0, t / 0.012)
            release = min(1.0, (duration - t) / 0.08)
            envelope = max(0.0, min(attack, release))
            wave_value = math.sin(phase) * 0.85 + math.sin(2.0 * phase) * 0.15
            result.append(int(max(-32767, min(32767, wave_value * envelope * AMPLITUDE))))
            phase += 2.0 * math.pi * frequency / SAMPLE_RATE
    return result


def main() -> None:
    samples = make_samples()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUTPUT), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(struct.pack(f"<{len(samples)}h", *samples))
    print(f"Wrote {OUTPUT} ({len(samples) / SAMPLE_RATE:.2f} seconds)")


if __name__ == "__main__":
    main()
