#!/bin/bash
# Run nrscope-sc2430, log to $1 (prepending timestamps to each line), 
# and start a watchdog to abort when an overflow is detected after sync. 

if [ ! -f "./nrscope-sc2430" ]; then
    echo "Error: nrscope-sc2430 not found in current directory"
    exit 1
fi

if [ -z "$1" ]; then
    echo "Usage: $0 <log_file>"
    exit 1
fi

LOG_FILE="$(realpath -m "$1")"

function nrscope {
    # Run nrscope with timestamps, logging to $LOG_FILE.
    # Watchdog runs in the background to detect post-sync overflows.
    watchdog &
    local watchdog_pid=$!
    ./nrscope-sc2430 2>&1 | awk '{ print "[" systime() "] " $0; fflush() }' > "$LOG_FILE"
    kill "$watchdog_pid" 2>/dev/null
}

function watchdog {
    # Check the log file every second, for the "Overflow" keyword after "in_sync".
    # If found, kill the nrscope process and exit with an error code.
    local seen_sync=0
    local sync_line=0
    while true; do
        sleep 1
        [ -f "$LOG_FILE" ] || continue
        if [ "$seen_sync" -eq 0 ]; then
            if grep -q "in_sync" "$LOG_FILE"; then
                seen_sync=1
                # Record line count at the point we saw in_sync, so we only
                # check for Overflow in lines that come after.
                sync_line=$(grep -n "in_sync" "$LOG_FILE" | tail -1 | cut -d: -f1)
            fi
        fi
        if [ "$seen_sync" -eq 1 ]; then
            cur_line=$(wc -l < "$LOG_FILE")
            tail -n +"$((sync_line + 1))" "$LOG_FILE" | grep -q "Overflow" && {
                echo "[ABORT] Overflow detected after in_sync"
                pkill -f nrscope-sc2430
                exit 1
            }
            sync_line=$cur_line
        fi
    done
}

nrscope




## Old
# ./nrscope-sc2430 2>&1 | awk '{ print "[" strftime("%Y-%m-%d %H:%M:%S") "] " $0; fflush() }' | tee "$LOG_FILE" | awk '
#     /masterCell|Overflow|in_sync|Overwriting|Cell Found|Decoding MIB|Found DCI|SIB 1 Decoded/ {
#         print; fflush()
#     }
#     /in_sync/ { seen_sync = 1 }
#     /Overflow/ && seen_sync {
#         print "[ABORT] Overflow detected after in_sync"; fflush()
#         exit 1
#     }
# '
