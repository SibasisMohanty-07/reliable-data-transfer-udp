
#!/usr/bin/env bash
set -u

cd "$(dirname "$0")/.." || exit 1

INPUT="week5_test.bin"
RECEIVED="received.txt"
RESULTS="experiments/gbn_results.csv"
TIMEOUT_SECONDS=90

mkdir -p experiments

for file in "$INPUT" sender_gbn_experiment.exe receiver.exe channel_proxy.exe; do
    if [[ ! -f "$file" ]]; then
        echo "ERROR: Missing $file"
        exit 1
    fi
done

# Compare binary files using SHA-256 hashes.
if ! command -v sha256sum >/dev/null 2>&1; then
    echo "ERROR: sha256sum is unavailable. Run this in MSYS2 UCRT64."
    exit 1
fi

compare_files() {
    local original_hash received_hash

    original_hash="$(sha256sum "$1" | cut -d ' ' -f 1)" || return 1
    received_hash="$(sha256sum "$2" | cut -d ' ' -f 1)" || return 1

    [[ "$original_hash" == "$received_hash" ]]
}

printf 'timestamp,protocol,scenario,loss,delay_ms,duplication,corruption,reordering,jitter_ms,transfer_ok,integrity_ok,elapsed_seconds\n' > "$RESULTS"

cleanup() {
    local pid="$1"
    if [[ -n "$pid" ]]; then
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    fi
}

run_test() {
    local scenario="$1"
    local loss="$2"
    local delay="$3"
    local duplication="$4"
    local corruption="$5"
    local reordering="$6"
    local jitter="$7"

    local receiver_pid="" proxy_pid=""
    local transfer_ok=0 integrity_ok=0 elapsed=0
    local start end

    local sender_log="experiments/gbn_sender_${scenario}.log"
    local receiver_log="experiments/gbn_receiver_${scenario}.log"
    local proxy_log="experiments/gbn_proxy_${scenario}.log"

    echo
    echo "=== Go-Back-N: $scenario ==="
    rm -f "$RECEIVED"

    ./receiver.exe > "$receiver_log" 2>&1 &
    receiver_pid=$!
    sleep 1

    if ! kill -0 "$receiver_pid" 2>/dev/null; then
        echo "ERROR: Receiver failed to start. See $receiver_log"
    else
        ./channel_proxy.exe "$loss" "$delay" "$duplication" \
            "$corruption" "$reordering" "$jitter" \
            > "$proxy_log" 2>&1 &
        proxy_pid=$!
        sleep 1

        if ! kill -0 "$proxy_pid" 2>/dev/null; then
            echo "ERROR: Proxy failed to start. See $proxy_log"
            cat "$proxy_log"
        else
            echo "Receiver and proxy started."
            echo "Running sender..."

            start=$(date +%s)

            if timeout "$TIMEOUT_SECONDS" \
                ./sender_gbn_experiment.exe "$INPUT" \
                > "$sender_log" 2>&1; then
                transfer_ok=1
            else
                echo "Sender failed or timed out. See $sender_log"
            fi

            end=$(date +%s)
            elapsed=$((end - start))
            sleep 2

            if [[ -f "$RECEIVED" ]] &&
               compare_files "$INPUT" "$RECEIVED"; then
                integrity_ok=1
                echo "Integrity check passed."
            else
                integrity_ok=0
                echo "Integrity check failed."
            fi
        fi
    fi

    cleanup "$proxy_pid"
    cleanup "$receiver_pid"

    printf '%s,GoBackN,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$(date -Iseconds)" "$scenario" "$loss" "$delay" \
        "$duplication" "$corruption" "$reordering" "$jitter" \
        "$transfer_ok" "$integrity_ok" "$elapsed" >> "$RESULTS"

    echo "Transfer success: $transfer_ok"
    echo "Integrity success: $integrity_ok"
    echo "Elapsed seconds: $elapsed"
    echo "Result saved to $RESULTS"

    if [[ "$transfer_ok" -eq 0 ]]; then
        echo "Stopping because this transfer failed. Inspect the logs."
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
echo "Go-Back-N experiments finished."
cat "$RESULTS"
