
#!/usr/bin/env bash
set -u

mkdir -p experiments

RESULTS="experiments/results.csv"
INPUT="week5_test.bin"
RECEIVED="received_sr.txt"
PROXY_LOG="experiments/channel_proxy.log"

# Track only processes started by this script.
receiver_pid=""
proxy_pid=""

cleanup() {
    if [[ -n "$proxy_pid" ]]; then
        kill "$proxy_pid" 2>/dev/null || true
        wait "$proxy_pid" 2>/dev/null || true
        proxy_pid=""
    fi

    if [[ -n "$receiver_pid" ]]; then
        kill "$receiver_pid" 2>/dev/null || true
        wait "$receiver_pid" 2>/dev/null || true
        receiver_pid=""
    fi
}

trap cleanup EXIT
trap 'echo "Interrupted."; exit 130' INT
trap 'echo "Terminated."; exit 143' TERM

if [[ ! -f "$INPUT" ]]; then
    echo "ERROR: $INPUT not found."
    echo "Run this script from the project root."
    exit 1
fi

if [[ ! -x "./sender_sr.exe" ||
      ! -x "./receiver_sr.exe" ||
      ! -x "./channel_proxy.exe" ]]; then
    echo "ERROR: Required executable missing."
    echo "Required: sender_sr.exe, receiver_sr.exe, channel_proxy.exe"
    exit 1
fi

printf 'timestamp,protocol,scenario,loss,delay_ms,duplication,corruption,reordering,jitter_ms,transfer_ok,integrity_ok,elapsed_seconds\n' > "$RESULTS"

run_test() {
    local scenario="$1"
    local loss="$2"
    local delay="$3"
    local duplication="$4"
    local corruption="$5"
    local reordering="$6"
    local jitter="$7"

    local start end elapsed transfer_ok integrity_ok
    local receiver_log="experiments/receiver_${scenario}.log"
    local proxy_run_log="experiments/proxy_${scenario}.log"
    local sender_log="experiments/sender_${scenario}.log"

    echo
    echo "=== Scenario: $scenario ==="

    cleanup
    rm -f "$RECEIVED"

    # Start the receiver first.
    ./receiver_sr.exe > "$receiver_log" 2>&1 &
    receiver_pid=$!
    sleep 1

    if ! kill -0 "$receiver_pid" 2>/dev/null; then
        echo "ERROR: Receiver failed to start. See $receiver_log"
        transfer_ok=0
        integrity_ok=0
        elapsed=0
        record_result
        return
    fi

    # Start the channel proxy.
    ./channel_proxy.exe "$loss" "$delay" "$duplication" \
        "$corruption" "$reordering" "$jitter" \
        > "$proxy_run_log" 2>&1 &
    proxy_pid=$!
    sleep 1

    if ! kill -0 "$proxy_pid" 2>/dev/null; then
        echo "ERROR: Proxy failed to start. See $proxy_run_log"
        transfer_ok=0
        integrity_ok=0
        elapsed=0
        cleanup
        record_result
        return
    fi

    start=$(date +%s)

    # timeout is available in MSYS2. If unavailable, this will fail
    # and be recorded rather than silently marking the test successful.
    if timeout 90 ./sender_sr.exe "$INPUT" > "$sender_log" 2>&1; then
        transfer_ok=1
    else
        transfer_ok=0
        echo "Sender failed or timed out. See $sender_log"
    fi

    end=$(date +%s)
    elapsed=$((end - start))

    # Stop the proxy and receiver before checking the output file.
    cleanup

    # Windows fc.exe performs a byte-for-byte comparison.
    # //c prevents MSYS2 from rewriting the Windows /c switch.
    if [[ -f "$RECEIVED" ]] &&
       cmd.exe //c "fc /b week5_test.bin received_sr.txt" \
           > "experiments/compare_${scenario}.log" 2>&1; then
        integrity_ok=1
    else
        integrity_ok=0
        echo "Integrity check failed. See experiments/compare_${scenario}.log"
    fi

    record_result

    echo "Transfer success: $transfer_ok"
    echo "Integrity success: $integrity_ok"
    echo "Elapsed seconds: $elapsed"
}

record_result() {
    printf '%s,SelectiveRepeat,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$(date -Iseconds)" "$scenario" "$loss" "$delay" \
        "$duplication" "$corruption" "$reordering" "$jitter" \
        "$transfer_ok" "$integrity_ok" "$elapsed" >> "$RESULTS"
}

run_test baseline      0   0   0 0   0 0
run_test loss_10       0.1 0   0 0   0 0
run_test corruption_10 0   0   0 0.1 0 0
run_test duplication   0   0   1 0   0 0
run_test reordering    0   0   0 0   1 0
run_test delay_jitter  0   100 0 0   0 100

echo
echo "Experiments complete. Results saved to $RESULTS"
echo
cat "$RESULTS"
