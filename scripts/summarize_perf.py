"""Summarize a closed ReXGlue CSV capture; use a gameplay-only frame range."""
import argparse
import csv
import json
import math
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--start-frame', type=int, default=0)
    parser.add_argument('--frames', type=int)
    args = parser.parse_args()
    if args.start_frame < 0 or (args.frames is not None and args.frames <= 0):
        parser.error('Frame offsets must be nonnegative and frame counts positive.')
    with args.capture.open(newline='', encoding='utf-8-sig') as stream:
        rows = list(csv.DictReader(stream))
    rows = rows[args.start_frame:None if args.frames is None else args.start_frame + args.frames]
    times = sorted(float(row['frame_time_us']) / 1000 for row in rows
                   if float(row['frame_time_us']) > 0)
    if not times:
        parser.error('No positive frame times in the selected range.')
    def percentile(p):
        return times[max(0, math.ceil(len(times) * p) - 1)]
    print(json.dumps({
        'capture': str(args.capture.resolve()),
        'start_frame': args.start_frame,
        'valid_frames': len(times),
        'measured_seconds': round(sum(times) / 1000, 3),
        'average_fps': round(1000 * len(times) / sum(times), 2),
        'frame_ms_p50': round(percentile(.5), 3),
        'frame_ms_p95': round(percentile(.95), 3),
        'frame_ms_p99': round(percentile(.99), 3),
        'note': 'Instrumented capture; label the scene and exclude startup/loading before comparing.'
    }, indent=2))


if __name__ == '__main__':
    main()
