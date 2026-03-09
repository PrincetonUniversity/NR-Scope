#!/bin/bash
# run nrscope. Log to $1. Abort when an overflow is detected after sync with tower.
# not currently used, because it might add overhead.
if [ -z "$1" ]; then
    echo "Usage: $0 <log_file>"
    exit 1
fi
LOG_FILE="$(realpath -m "$1")"

./nrscope-sc2430 2>&1 | tee "$LOG_FILE" | awk '
    /masterCell|Overflow|in_sync|Overwriting|Cell Found|Decoding MIB|SIB 1 Decoded/ {
        print "[" strftime("%Y-%m-%d %H:%M:%S") "] " $0; fflush()
    }
    /in_sync/ { seen_sync = 1 }
    /Overflow/ && seen_sync {
        print "[ABORT] Overflow detected after in_sync"; fflush()
        exit 1
    }
'
