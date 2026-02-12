#!/bin/bash

# tcpping.sh: Bash TCP ping client using /dev/tcp (long connection)
# Usage: tcpping.sh [-c count] [-i interval] [-w timeout] host [port]

# Default values
COUNT=-1  # -1 means infinite
INTERVAL=1
TIMEOUT=5
PORT=50414

# Parse options (simple getopts, no long opts)
while getopts ":c:i:w:" opt; do
  case $opt in
    c) COUNT="$OPTARG" ;;
    i) INTERVAL="$OPTARG" ;;
    w) TIMEOUT="$OPTARG" ;;
    \?) echo "Invalid option: -$OPTARG" >&2; exit 1 ;;
  esac
done
shift $((OPTIND-1))

HOST="${1:?Missing host}"
PORT="${2:-$PORT}"

# Check for bc
if command -v bc >/dev/null 2>&1; then
  BC_AVAILABLE=true
else
  BC_AVAILABLE=false
  echo "Warning: 'bc' not found. Using integer math (less precise)."
fi

# Check date nanoseconds support (fallback to milliseconds if not)
if date +%s%N >/dev/null 2>&1; then
  TIME_CMD="date +%s%N"
  NANO_FACTOR=1000000
else
  # macOS or others: use perl for ms precision
  if command -v perl >/dev/null 2>&1; then
    TIME_CMD="perl -MTime::HiRes -e 'printf(\"%.0f\n\", Time::HiRes::time() * 1000)'"
    NANO_FACTOR=1000  # Actually ms, but we treat as "nano" for calc
  else
    echo "Error: Need 'date +%s%N' or 'perl' for timing." >&2
    exit 1
  fi
fi

echo "TCP Pinging $HOST:$PORT (count: ${COUNT/-1/infinite}, interval: $INTERVAL s, timeout: $TIMEOUT s)..."
echo "Press Ctrl+C to stop."

# Trap Ctrl+C
trap 'echo -e "\nCaught Ctrl+C, exiting..."; running=0' INT

# Open connection
exec 3<>/dev/tcp/$HOST/$PORT
if [ $? -ne 0 ]; then
  echo "Failed to connect to $HOST:$PORT" >&2
  exit 1
fi

# Stats
transmitted=0
received=0
total_rtt_nano=0
min_rtt_nano=999999999999
max_rtt_nano=0
running=1

while [ $running -eq 1 ] && [ $COUNT -eq -1 -o $transmitted -lt $COUNT ]; do
  start_time=$($TIME_CMD)
  echo -n "PING" >&3
  if [ $? -ne 0 ]; then
    echo "Send failed (connection broken?)" >&2
    break
  fi

  transmitted=$((transmitted + 1))

  # Read response with timeout
  if read -t $TIMEOUT -u 3 response; then
    end_time=$($TIME_CMD)

    # Trim response (remove \r\n and spaces)
    response=$(echo "$response" | tr -d '\r\n[:space:]')

    # Parse if has timestamp (PONG<timestamp>)
    server_ts=-1
    if [[ $response =~ ^PONG([0-9]+)$ ]]; then
      server_ts=${BASH_REMATCH[1]}
    fi

    # Calc RTT (prefer server ts if available, else local diff)
    if [ $server_ts -ne -1 ]; then
      # Assume server_ts is usec, adjust if needed
      rtt_nano=$(( (end_time - start_time) + (start_time - server_ts * (NANO_FACTOR / 1000)) ))  # Adjust units
    else
      rtt_nano=$((end_time - start_time))
    fi

    if [ $rtt_nano -gt 0 ]; then
      received=$((received + 1))
      total_rtt_nano=$((total_rtt_nano + rtt_nano))
      [ $rtt_nano -lt $min_rtt_nano ] && min_rtt_nano=$rtt_nano
      [ $rtt_nano -gt $max_rtt_nano ] && max_rtt_nano=$rtt_nano

      if $BC_AVAILABLE; then
        rtt_ms=$(echo "scale=3; $rtt_nano / $NANO_FACTOR" | bc)
      else
        rtt_ms=$((rtt_nano / NANO_FACTOR))
      fi
      echo "Reply from $HOST:$PORT: seq=$transmitted time=$rtt_ms ms"
    else
      echo "Invalid RTT (bad timestamp?)"
    fi
  else
    echo "Timeout for seq=$transmitted"
  fi

  # Sleep interval (but check running)
  sleep $INTERVAL
done

# Close connection
exec 3<&-
exec 3>&-

# Print stats
echo -e "\n--- $HOST:$PORT tcpping statistics ---"
echo "$transmitted packets transmitted, $received received"

if [ $received -gt 0 ]; then
  if $BC_AVAILABLE; then
    avg_rtt_ms=$(echo "scale=3; $total_rtt_nano / $received / $NANO_FACTOR" | bc)
    min_rtt_ms=$(echo "scale=3; $min_rtt_nano / $NANO_FACTOR" | bc)
    max_rtt_ms=$(echo "scale=3; $max_rtt_nano / $NANO_FACTOR" | bc)
  else
    avg_rtt_ms=$((total_rtt_nano / received / NANO_FACTOR))
    min_rtt_ms=$((min_rtt_nano / NANO_FACTOR))
    max_rtt_ms=$((max_rtt_nano / NANO_FACTOR))
  fi
  echo "rtt min/avg/max = $min_rtt_ms / $avg_rtt_ms / $max_rtt_ms ms"
else
  echo "No successful pings."
fi
