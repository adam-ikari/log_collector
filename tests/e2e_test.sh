#!/bin/bash
# End-to-end test for log_collector
set -e

BINARY="./log_collector"
# 日志目录由 config.h 中的 CFG_LOG_DIR 决定（默认 /tmp/log_collector_test）
LOG_DIR="/tmp/log_collector_test"
SHM_NAME="/dev/shm/log_collector_shm"
TODAY=$(date +%Y-%m-%d)
PASS=0
FAIL=0

cleanup() {
    kill $COLLECTOR_PID 2>/dev/null || true
    wait $COLLECTOR_PID 2>/dev/null || true
    sleep 0.5
    rm -rf "$LOG_DIR" 2>/dev/null || true
    rm -f "$SHM_NAME" 2>/dev/null || true
}

# Start collector and verify it's running. Exits test on failure.
start_collector() {
    local desc="$1"
    $BINARY -f &
    COLLECTOR_PID=$!
    sleep 1
    if ! kill -0 $COLLECTOR_PID 2>/dev/null; then
        echo "  FAIL: Collector failed to start ($desc)"
        FAIL=$((FAIL + 1))
        cleanup
        exit 1
    fi
}

# Stop collector and wait for full cleanup
stop_collector() {
    kill -TERM $COLLECTOR_PID 2>/dev/null || true
    for i in $(seq 1 30); do
        if ! kill -0 $COLLECTOR_PID 2>/dev/null; then
            break
        fi
        sleep 0.2
    done
    kill -9 $COLLECTOR_PID 2>/dev/null || true
    wait $COLLECTOR_PID 2>/dev/null || true
    sleep 0.5
    fuser -k 5140/tcp 2>/dev/null || true
    fuser -k 5140/udp 2>/dev/null || true
    sleep 0.3
    rm -f "$SHM_NAME" 2>/dev/null || true
    sleep 0.3
}

assert_file_contains() {
    local file="$1"
    local pattern="$2"
    local desc="$3"
    if grep -q "$pattern" "$file" 2>/dev/null; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc (pattern '$pattern' not found in $file)"
        echo "  File content: $(cat "$file" 2>/dev/null || echo '(empty)')"
        FAIL=$((FAIL + 1))
    fi
}

assert_dir_exists() {
    local dir="$1"
    local desc="$2"
    if [ -d "$dir" ]; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc (directory $dir does not exist)"
        FAIL=$((FAIL + 1))
    fi
}

echo "============================================"
echo "  Log Collector - End-to-End Test Suite"
echo "============================================"

# Pre-test cleanup
pkill -9 -f "(^|/)log_collector[[:space:]]" 2>/dev/null || true
sleep 1
fuser -k 5140/tcp 2>/dev/null || true
fuser -k 5140/udp 2>/dev/null || true
sleep 0.5
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null || true

# --- Test 1: Startup and shutdown ---
echo ""
echo "[Test 1] Startup / graceful shutdown"
$BINARY -f &
COLLECTOR_PID=$!
sleep 1

if kill -0 $COLLECTOR_PID 2>/dev/null; then
    echo "  PASS: Collector started successfully (PID=$COLLECTOR_PID)"
    PASS=$((PASS + 1))
else
    echo "  FAIL: Collector failed to start"
    FAIL=$((FAIL + 1))
    cleanup
    exit 1
fi

kill -TERM $COLLECTOR_PID
for i in $(seq 1 50); do
    if ! kill -0 $COLLECTOR_PID 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if kill -0 $COLLECTOR_PID 2>/dev/null; then
    echo "  FAIL: Collector did not shut down on SIGTERM"
    FAIL=$((FAIL + 1))
    kill -9 $COLLECTOR_PID 2>/dev/null || true
else
    echo "  PASS: Collector shut down gracefully"
    PASS=$((PASS + 1))
fi

rm -f "$SHM_NAME" 2>/dev/null || true

# --- Test 2: UDP single message ---
echo ""
echo "[Test 2] Single UDP message"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 2"

echo "<13>Jun 18 14:32:05 myhost myapp[1234]: UDP connection timeout" | nc -u -w1 127.0.0.1 5140
sleep 0.5

LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
assert_file_contains "$LOG_FILE" "connection timeout" "UDP message written to correct IP/date file"
assert_file_contains "$LOG_FILE" "\[notice\]" "UDP message has correct severity (notice=5)"
assert_file_contains "$LOG_FILE" "127.0.0.1" "UDP message contains client IP"

stop_collector

# --- Test 3: TCP single message ---
echo ""
echo "[Test 3] Single TCP message"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 3"

for i in $(seq 1 10); do
    if ss -tlnp 2>/dev/null | grep -q 5140; then
        break
    fi
    sleep 0.1
done

echo "<14>TCP info message from smoke test" | nc -q1 127.0.0.1 5140
sleep 0.5

LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
assert_file_contains "$LOG_FILE" "smoke test" "TCP message written to file"
assert_file_contains "$LOG_FILE" "\[info\]" "TCP message has correct severity (info=6)"

stop_collector

# --- Test 4: Multiple messages, batch test ---
echo ""
echo "[Test 4] Batch messages (TCP + UDP mixed)"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 4"

for i in $(seq 1 10); do
    echo "<$((13 + i % 7))>TCP batch message #$i" | nc -q1 127.0.0.1 5140
done

for i in $(seq 1 10); do
    echo "<$((10 + i % 7))>UDP batch message #$i" | nc -u -w1 127.0.0.1 5140
done

sleep 1

LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
MSG_COUNT=$(grep -c "batch message" "$LOG_FILE" 2>/dev/null || echo 0)
if [ "$MSG_COUNT" -ge 18 ]; then
    echo "  PASS: Batch test: $MSG_COUNT messages received (>=18 expected)"
    PASS=$((PASS + 1))
else
    echo "  FAIL: Batch test: only $MSG_COUNT messages received (>=18 expected)"
    FAIL=$((FAIL + 1))
fi

TCP_COUNT=$(grep -c "TCP batch" "$LOG_FILE" 2>/dev/null || echo 0)
UDP_COUNT=$(grep -c "UDP batch" "$LOG_FILE" 2>/dev/null || echo 0)
if [ "$TCP_COUNT" -ge 8 ] && [ "$UDP_COUNT" -ge 8 ]; then
    echo "  PASS: TCP/UDP both received (TCP=$TCP_COUNT, UDP=$UDP_COUNT)"
    PASS=$((PASS + 1))
else
    echo "  FAIL: TCP/UDP count mismatch (TCP=$TCP_COUNT, UDP=$UDP_COUNT)"
    FAIL=$((FAIL + 1))
fi

stop_collector

# --- Test 5: Multi-client IP separation ---
echo ""
echo "[Test 5] Multi-client IP separation"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 5"

echo "<13>message from client A" | nc -u -w1 127.0.0.1 5140
echo "<14>message from client B" | nc -u -w1 127.0.0.1 5140
sleep 0.5

assert_dir_exists "$LOG_DIR/127.0.0.1" "Client IP directory exists"
LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
MSG_A=$(grep -c "client A" "$LOG_FILE" 2>/dev/null || echo 0)
MSG_B=$(grep -c "client B" "$LOG_FILE" 2>/dev/null || echo 0)
if [ "$MSG_A" -ge 1 ] && [ "$MSG_B" -ge 1 ]; then
    echo "  PASS: Both client messages received in same IP directory"
    PASS=$((PASS + 1))
else
    echo "  FAIL: Missing messages for client A($MSG_A) or B($MSG_B)"
    FAIL=$((FAIL + 1))
fi

stop_collector

# --- Test 6: Syslog PRI (severity) parsing ---
echo ""
echo "[Test 6] Syslog severity level parsing"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 6"

echo "<0>EMERG: system panic" | nc -u -w1 127.0.0.1 5140
echo "<1>ALERT: immediate action" | nc -u -w1 127.0.0.1 5140
echo "<2>CRIT: critical condition" | nc -u -w1 127.0.0.1 5140
echo "<3>ERR: error condition" | nc -u -w1 127.0.0.1 5140
echo "<4>WARNING: warning condition" | nc -u -w1 127.0.0.1 5140
echo "<5>NOTICE: normal but significant" | nc -u -w1 127.0.0.1 5140
echo "<6>INFO: informational" | nc -u -w1 127.0.0.1 5140
echo "<7>DEBUG: debug message" | nc -u -w1 127.0.0.1 5140
sleep 0.5

LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
assert_file_contains "$LOG_FILE" "\[emerg\]" "Severity 0 -> emerg"
assert_file_contains "$LOG_FILE" "\[alert\]" "Severity 1 -> alert"
assert_file_contains "$LOG_FILE" "\[crit\]"  "Severity 2 -> crit"
assert_file_contains "$LOG_FILE" "\[err\]"   "Severity 3 -> err"
assert_file_contains "$LOG_FILE" "\[warning\]" "Severity 4 -> warning"
assert_file_contains "$LOG_FILE" "\[notice\]"  "Severity 5 -> notice"
assert_file_contains "$LOG_FILE" "\[info\]"    "Severity 6 -> info"
assert_file_contains "$LOG_FILE" "\[debug\]"   "Severity 7 -> debug"

stop_collector

# --- Test 7: High throughput stress ---
echo ""
echo "[Test 7] High throughput stress (1000 messages)"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 7"

# UDP: 500 messages (fast, fire-and-forget)
for i in $(seq 1 500); do
    echo "<14>stress test message #$i" | nc -u -w0 127.0.0.1 5140
done

# TCP: 500 messages via background parallel sends (avoid serial nc -q1 timeout)
for i in $(seq 1 500); do
    (echo "<14>stress test message #$((i + 500))" | nc -q1 127.0.0.1 5140) &
done
# Wait for all background nc processes with a 30s timeout
for i in $(seq 1 30); do
    if ! pgrep -f "nc.*127.0.0.1 5140" > /dev/null 2>&1; then
        break
    fi
    sleep 1
done
# Kill any stragglers
pkill -f "nc.*127.0.0.1 5140" 2>/dev/null || true
sleep 2

LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
STRESS_COUNT=$(grep -c "stress test message" "$LOG_FILE" 2>/dev/null || echo 0)
if [ "$STRESS_COUNT" -ge 900 ]; then
    echo "  PASS: Stress test: $STRESS_COUNT/1000 messages received"
    PASS=$((PASS + 1))
else
    echo "  FAIL: Stress test: only $STRESS_COUNT/1000 messages received"
    FAIL=$((FAIL + 1))
fi

stop_collector

# --- Test 8: Worker crash recovery ---
echo ""
echo "[Test 8] Worker crash recovery"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 8"

WORKER_PIDS=$(pgrep -P $COLLECTOR_PID)
WORKER_COUNT=$(echo "$WORKER_PIDS" | wc -w)
echo "  INFO: Found $WORKER_COUNT worker process(es)"

if [ "$WORKER_COUNT" -gt 0 ]; then
    FIRST_WORKER=$(echo "$WORKER_PIDS" | head -1)
    kill -9 "$FIRST_WORKER" 2>/dev/null
    sleep 1

    NEW_WORKER_COUNT=$(pgrep -P $COLLECTOR_PID | wc -w)
    if [ "$NEW_WORKER_COUNT" -ge "$WORKER_COUNT" ]; then
        echo "  PASS: Worker was restarted after crash ($NEW_WORKER_COUNT workers)"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: Worker not restarted (was $WORKER_COUNT, now $NEW_WORKER_COUNT)"
        FAIL=$((FAIL + 1))
    fi

    echo "<14>message after worker crash" | nc -u -w1 127.0.0.1 5140
    sleep 0.5
    LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
    assert_file_contains "$LOG_FILE" "after worker crash" "System functional after worker crash"
else
    echo "  SKIP: No workers found (worker_count may be set to 0?)"
fi

stop_collector

# --- Test 9: No PRI prefix (default severity) ---
echo ""
echo "[Test 9] Messages without PRI prefix"
rm -rf "$LOG_DIR" "$SHM_NAME" 2>/dev/null
start_collector "Test 9"

echo "plain text message without any priority" | nc -u -w1 127.0.0.1 5140
sleep 0.5

LOG_FILE="$LOG_DIR/127.0.0.1/$TODAY.log"
assert_file_contains "$LOG_FILE" "plain text message" "Message without PRI received"
assert_file_contains "$LOG_FILE" "\[debug\]" "No-PRI message defaults to debug severity"

stop_collector

# --- Summary ---
echo ""
echo "============================================"
echo "  Results: $PASS passed, $FAIL failed"
echo "============================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
