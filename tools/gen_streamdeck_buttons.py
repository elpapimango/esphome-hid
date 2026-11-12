#!/usr/bin/env python3
"""Prints the 32 `button:` entries of a Stream Deck XL (4 rows x 8 columns), named "Key R<row>C<column>" so that the key in
Home Assistant is easy to find on the grid. Paste the output under `button:` in your configuration.

    python tools/gen_streamdeck_buttons.py [--prefix "Key"] [--rows 4] [--columns 8]
"""
import argparse

ap = argparse.ArgumentParser()
ap.add_argument("--prefix", default="Key")
ap.add_argument("--rows", type=int, default=4)
ap.add_argument("--columns", type=int, default=8)
args = ap.parse_args()
for row in range(args.rows):
    for col in range(args.columns):
        print(f'  - platform: hid_streamdeck\n    name: "{args.prefix} R{row}C{col}"\n    row: {row}\n    column: {col}')
