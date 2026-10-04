#!/bin/sh
# Run these on your Mac before taking screenshots. No Python needed to run C.
set -eu
cd "$(dirname "$0")"
cc -std=c11 -Wall -Wextra -Wpedantic -pthread buffer.c -o a.out
mkdir -p evidence screenshots
./a.out 20 5 5 > evidence/mac_01_required.txt
./a.out 12 1 1 > evidence/mac_02_single.txt
./a.out 12 5 1 > evidence/mac_03_producers.txt
./a.out 12 1 5 > evidence/mac_04_consumers.txt
./a.out 12 3 3 > evidence/mac_05_balanced.txt
printf 'Five runs completed. Full logs are in evidence/mac_*.txt.\n'
printf 'Take the five Terminal screenshots using START_HERE_MAC.md.\n'
