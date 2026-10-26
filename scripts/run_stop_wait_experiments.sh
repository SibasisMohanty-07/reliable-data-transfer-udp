#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/.." || exit 1

INPUT="week5_test.bin"
RECEIVED="received_sw.txt"
RESULTS="experiments/stop_wait_results.csv"
TIMEOUT_SECONDS=90
mkdir -p experiments

for file in "$INPUT" sender_stop_wait.exe receiver_stop_wait.exe channel_proxy.exe; do
    if [[ ! -f "$file" ]]; then
        echo "ERROR: Missing $file"
        exit 1
    fi
done

compare_files() {
    local a b
    a="$(sha256sum "$1" | cut -d ' ' -f 1)" || return 1
    b="$(sha256sum "$2" | cut -d ' ' -f 1)" || return 1
    [[ "$a" == "$b" ]]
}

printf 'timestamp,protocol,scenario,loss,delay_ms,duplication,corruption,reordering,jitter_ms,transfer_ok,integrity_ok,elapsed_seconds\n' > "$RESULTS"

run_test() {
    local scenario="$1" loss="$2" delay="$3" duplication="$4"
    local corruption="$5" reordering="$6" jitter="$7"
    local receiver_pid="" proxy_pid="" transfer_ok=0 integrity_ok=0 elapsed=0
    local start end
    local sl="experiments/sw_sender_${scenario}.log"
    local rl="experiments/sw_receiver_${scenario}.log"
    local pl="experiments/sw_proxy_${scenario}.log"

    echo
    echo "=== Stop-and-Wait: $scenario ==="
    rm -f "$RECEIVED"

    ./receiver_stop_wait.exe > "$rl" 2>&1 &
    receiver_pid=$!
    sleep 1

    if ! kill -0 "$receiver_pid" 2>/dev/null; then
        echo "ERROR: Receiver failed. See $rl"
    else
        ./channel_proxy.exe "$loss" "$delay" "$duplication" \
            "$corruption" "$reordering" "$jitter" > "$pl" 2>&1 &
        proxy_pid=$!
        sleep 1

        if ! kill -0 "$proxy_pid" 2>/dev/null; then
            echo "ERROR: Proxy failed. See $pl"
        else
            echo "Receiver and proxy started; running sender..."
            start=$(date +%s)
            if timeout "$TIMEOUT_SECONDS" ./sender_stop_wait.exe "$INPUT" > "$sl" 2>&1; then
                transfer_ok=1
            else
                echo "Sender failed/timed out. See $sl"
            fi
            end=$(date +%s)
            elapsed=$((end-start))
            sleep 1
            if [[ -f "$RECEIVED" ]] && compare_files "$INPUT" "$RECEIVED"; then
                integrity_ok=1
                echo "Integrity check passed."
            else
                echo "Integrity check failed."
            fi
        fi
    fi

    if [[ -n "$proxy_pid" ]]; then kill "$proxy_pid" 2>/dev/null || true; wait "$proxy_pid" 2>/dev/null || true; fi
    if [[ -n "$receiver_pid" ]]; then kill "$receiver_pid" 2>/dev/null || true; wait "$receiver_pid" 2>/dev/null || true; fi

    printf '%s,StopAndWait,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$(date -Iseconds)" "$scenario" "$loss" "$delay" "$duplication" \
        "$corruption" "$reordering" "$jitter" "$transfer_ok" "$integrity_ok" "$elapsed" >> "$RESULTS"

    echo "Transfer success: $transfer_ok"
    echo "Integrity success: $integrity_ok"
    echo "Elapsed seconds: $elapsed"
    if [[ "$transfer_ok" -eq 0 ]]; then
        echo "Stopping after failure; inspect the scenario logs."
        return 1
    fi
    return 0
}

run_test baseline       0   0   0 0   0 0 || exit 1
run_test loss_10        0.1 0   0 0   0 0 || exit 1
run_test corruption_10  0   0   0 0.1 0 0 || exit 1
run_test duplication    0   0   1 0   0 0 || exit 1
run_test reordering     0   0   0 0   1 0 || exit 1
run_test delay_jitter   0   100 0 0   0 100 || exit 1

echo
echo "Stop-and-Wait experiments finished."
cat "$RESULTS"
