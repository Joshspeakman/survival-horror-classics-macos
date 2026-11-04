#!/usr/bin/env python3
"""gta3_art.py <art folder> [--no-downloads]: GTA III's launch window art; kept for older scripts, see gta_art.py."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gta_art

if __name__ == '__main__':
    if len(sys.argv) < 2: sys.exit(__doc__)
    sys.argv = [sys.argv[0], 'gta3'] + sys.argv[1:]
    sys.exit(gta_art.main())
