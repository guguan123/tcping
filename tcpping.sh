#!/bin/bash

# tcpping.sh: A bash-only TCP ping client using /dev/tcp
# Usage: tcpping.sh <host> <port> [count]

HOST=$1
PORT=${2:-50414}
COUNT=${3:-5} # Default to 5 ping if count is not provided
TIMEOUT=${4:-5} # Default timeout for read operations
INTERVAL=${5:-1} # Default interval between pings in seconds

# Check if 'bc' is available
if ! command -v bc &> /dev/null; then
    BC_AVAILABLE=false
    echo "Warning: 'bc' command not found. Latency will be calculated using integer division, which may reduce precision. For more precise results, please install 'bc'."
else
    BC_AVAILABLE=true
fi

if [ -z "$HOST" ] || [ -z "$PORT" ]; then
    echo "Usage: $0 <host> <port> [count] [timeout] [interval]"
    exit 1
fi

echo "TCP Pinging $HOST:$PORT with $COUNT attempts (timeout: ${TIMEOUT}s, interval: ${INTERVAL}s)..."

# Establish the TCP connection once
# The timeout option for 'connect' relies on the system's default TCP connect timeout.
(exec 3<>/dev/tcp/$HOST/$PORT) &> /dev/null
CONNECT_STATUS=$?

if [ $CONNECT_STATUS -ne 0 ]; then
    echo "Failed to establish initial connection to $HOST:$PORT"
    exit 1
fi
echo "Connection established to $HOST:$PORT (file descriptor 3)"

# Statistics variables
SUCCESS_COUNT=0
TOTAL_LATENCY_NANO=0
MIN_LATENCY_NANO=-1
MAX_LATENCY_NANO=0

for i in $(seq 1 $COUNT); do
    # Placeholder for PING/PONG logic
    START_TIME=$(date +%s%N) # Get nanosecond precision time
    echo -ne "PING\n" >&3 # Send PING to the server
    
    # Check if send failed (e.g., broken pipe)
    if [ $? -ne 0 ]; then
        echo "Error: Failed to send PING to $HOST:$PORT. Connection might be closed."
        break # Exit loop on send failure
    fi
    
    RESPONSE=""
    if ! read -t $TIMEOUT -u 3 RESPONSE; then
        if [ $? -eq 1 ]; then
            echo "Timeout waiting for PONG from $HOST:$PORT (seq=$i)"
        else
            echo "Error reading from $HOST:$PORT (seq=$i)"
        fi
        continue # Skip to next ping if read failed
    fi
    END_TIME=$(date +%s%N)
    
    # Trim whitespace and newline from response
    RESPONSE=$(echo "$RESPONSE" | tr -d '\r\n')

    if [ "$RESPONSE" == "PONG" ]; then
        LATENCY_NANO=$((END_TIME - START_TIME))
        if [ "$BC_AVAILABLE" = false ]; then
            LATENCY_MS=$((LATENCY_NANO / 1000000))
        else
            LATENCY_MS=$(echo "scale=2; $LATENCY_NANO / 1000000" | bc)
        fi
        echo "Reply from $HOST:$PORT: seq=$i time=${LATENCY_MS}ms"

        # Update statistics
        SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
        TOTAL_LATENCY_NANO=$((TOTAL_LATENCY_NANO + LATENCY_NANO))
        if [ "$MIN_LATENCY_NANO" -eq -1 ] || [ "$LATENCY_NANO" -lt "$MIN_LATENCY_NANO" ]; then
            MIN_LATENCY_NANO=$LATENCY_NANO
        fi
        if [ "$LATENCY_NANO" -gt "$MAX_LATENCY_NANO" ]; then
            MAX_LATENCY_NANO=$LATENCY_NANO
        fi
    else
        echo "Unexpected response from $HOST:$PORT: '$RESPONSE' (seq=$i)"
    fi

    sleep $INTERVAL


done

# Print statistics
echo ""
echo "--- $HOST tcpping statistics ---"
echo "$COUNT packets transmitted, $SUCCESS_COUNT received"

if [ "$SUCCESS_COUNT" -gt 0 ]; then
    if [ "$BC_AVAILABLE" = false ]; then
        AVG_LATENCY_MS=$((TOTAL_LATENCY_NANO / SUCCESS_COUNT / 1000000))
        MIN_LATENCY_MS=$((MIN_LATENCY_NANO / 1000000))
        MAX_LATENCY_MS=$((MAX_LATENCY_NANO / 1000000))
        echo "rtt min/avg/max = ${MIN_LATENCY_MS}ms/${AVG_LATENCY_MS}ms/${MAX_LATENCY_MS}ms"
    else
        AVG_LATENCY_MS=$(echo "scale=3; $TOTAL_LATENCY_NANO / $SUCCESS_COUNT / 1000000" | bc)
        MIN_LATENCY_MS=$(echo "scale=3; $MIN_LATENCY_NANO / 1000000" | bc)
        MAX_LATENCY_MS=$(echo "scale=3; $MAX_LATENCY_NANO / 1000000" | bc)
        echo "rtt min/avg/max = ${MIN_LATENCY_MS}ms/${AVG_LATENCY_MS}ms/${MAX_LATENCY_MS}ms"
    fi
else
    echo "No successful pings."
fi

# Close the file descriptor when done
exec 3<&-
exec 3>&-

echo "Connection closed."
