#!/usr/bin/env bash

# Default values
HOST="neko.guguan.us.kg"
PORT=50414
COUNT=-1  # -1 means infinite
INTERVAL=1
TIMEOUT=5
running=1

# --- 参数解析 ---
usage() {
	echo "Usage: $0 [-c count] [-i interval] [-w timeout] <host> [port]"
	exit 1
}

while getopts "c:i:w:" opt; do
	case $opt in
		c) COUNT="$OPTARG" ;;
		i) INTERVAL="$OPTARG" ;;
		w) TIMEOUT="$OPTARG" ;;
		*) usage ;;
	esac
done
shift $((OPTIND-1))

# --- 封装计算工具 ---
calc() {
	local expr="$1"
	if command -v awk >/dev/null 2>&1; then
		awk "BEGIN {printf $expr}"
	elif command -v bc >/dev/null 2>&1; then
		echo "scale=3; $expr" | bc
	else
		echo $(( expr ))
	fi
}

# 比较函数：返回较大或较小的值
# 用法: fmax 1.2 3.4 -> 输出 3.4
fmax() {
	local a="$1" b="$2"
	[[ -z "$b" ]] && b=0

	if command -v awk >/dev/null 2>&1; then
		awk "BEGIN {print ($a > $b ? $a : $b)}"
	elif command -v bc >/dev/null 2>&1; then
		echo "if ($a > $b) $a else $b" | bc
	else
		[ ${a%.*} -gt ${b%.*} ] && echo "$a" || echo "$b"
	fi
}

fmin() {
	local a="$1" b="$2"
	[[ -z "$b" ]] && { echo "$a"; return; }

	if command -v awk >/dev/null 2>&1; then
		awk "BEGIN {print ($a < $b ? $a : $b)}"
	elif command -v bc >/dev/null 2>&1; then
		echo "if ($a < $b) $a else $b" | bc
	else
		[ "${1%.*}" -lt "${2%.*}" ] && echo "$a" || echo "$b"
	fi
}

# 检查必填参数
HOST="${1:-$HOST}"
PORT="${2:-$PORT}"

# 建立连接
if ! exec 3<>/dev/tcp/$HOST/$PORT; then
	echo "Failed to connect to $HOST:$PORT"
	exit 1
fi

echo "Connected to $HOST:$PORT..."
echo "(count: ${COUNT/-1/infinite}, interval: $INTERVAL s, timeout: $TIMEOUT s)"
echo "Press Ctrl+C to stop."

# Trap Ctrl+C
trap 'echo -e "\nCaught Ctrl+C, exiting..."; running=0' INT

# --- 主循环 ---
transmitted=0
while [[ $running -eq 1 ]] && { [[ $COUNT -eq -1 ]] || [[ $transmitted -lt $COUNT ]]; }; do
	# 记录发送前的时间 (单位：纳秒)
	# %s 是秒，%N 是纳秒
	start_time=$(date +%s%N)

	# 发送 PING
	if ! echo -e "PING" >&3; then
		echo "Send failed"
		break
	fi

	# 等待 PONG
	if read -r -t $TIMEOUT response <&3; then
		transmitted=$((transmitted + 1))
		# 记录收到回复的时间
		end_time=$(date +%s%N)
		
		# 计算当前 RTT (纳秒转毫秒)
		duration_ms=$(calc "($end_time - $start_time) / 1000000")
		
		echo "Reply from $HOST: seq=$transmitted time=$duration_ms ms"

		# 更新统计数据
		if [ $transmitted -eq 1 ]; then
			total_rtt_ms=max_rtt_ms=min_rtt_ms=$duration_ms
		else
			total_rtt_ms=$(calc "$total_rtt_ms + $duration_ms")
			max_rtt_ms=$(fmax "$duration_ms" "${max_rtt_ms:-0}")
			min_rtt_ms=$(fmin "$duration_ms" "$min_rtt_ms")
		fi
		[[ $running -eq 1 ]] && sleep "$INTERVAL"
	else
		echo "Timeout for seq=$transmitted"
		break
	fi
done

exec 3>&-

# --- 统计输出 ---
if [ $transmitted -gt 0 ]; then
	echo -e "\n--- $HOST:$PORT tcpping statistics ---"
	echo "$transmitted packets transmitted"
	avg_rtt_ms=$(calc "$total_rtt_ms / $transmitted")
	echo "rtt min/avg/max = $min_rtt_ms / $avg_rtt_ms / $max_rtt_ms ms"
fi
