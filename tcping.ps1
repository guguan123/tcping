param (
	[string]$HostName = "neko.guguan.us.kg",
	[int]$Port = 50414,
	[int]$Count = -1,    # -1 表示无限次
	[int]$Interval = 1,  # 间隔秒数
	[int]$Timeout = 5    # 超时秒数
)

$transmitted = 0
$totalRtt = 0
$minRtt = [double]::MaxValue
$maxRtt = 0
$running = $true

# 处理 Ctrl+C

Write-Host "Connecting to ${HostName}:${Port}..." -ForegroundColor Cyan
Write-Host "(count: $(if($Count -eq -1){"infinite"}else{$Count}), interval: $Interval s, timeout: $Timeout s)"

$client = New-Object System.Net.Sockets.TcpClient
try {
	$client.Connect($HostName, $Port)
	$stream = $client.GetStream()
	$writer = New-Object System.IO.StreamWriter($stream)
	$reader = New-Object System.IO.StreamReader($stream)
	$writer.AutoFlush = $true

	while ($running -and ($Count -eq -1 -or $transmitted -lt $Count)) {
		$startTime = [DateTime]::Now
		
		try {
			# 发送 PING
			$writer.WriteLine("PING")
			
			# 等待响应（带超时逻辑）
			if ($client.Client.Poll($Timeout * 1000000, [System.Net.Sockets.SelectMode]::SelectRead)) {
				$response = $reader.ReadLine()
				$endTime = [DateTime]::Now
				
				if ($null -ne $response) {
					$transmitted++
					$rtt = ($endTime - $startTime).TotalMilliseconds
					
					# 更新统计
					$totalRtt += $rtt
					if ($rtt -lt $minRtt) { $minRtt = $rtt }
					if ($rtt -gt $maxRtt) { $maxRtt = $rtt }

					Write-Host ("Reply from {0}: seq={1} time={2:F3} ms" -f $HostName, $transmitted, $rtt)
				}
			} else {
				Write-Host "Timeout for seq=$($transmitted + 1)" -ForegroundColor Red
				break
			}
		} catch {
			Write-Host "Connection error!" -ForegroundColor Red
			break
		}

		if ($running) { Start-Sleep -Seconds $Interval }
	}
} catch {
	Write-Host "Failed to connect to ${HostName}:${Port}" -ForegroundColor Red
} finally {
	# 打印统计信息
	if ($transmitted -gt 0) {
		$avgRtt = $totalRtt / $transmitted
		Write-Host "`n--- ${HostName}:${Port} tcpping statistics ---" -ForegroundColor Cyan
		Write-Host "$transmitted packets transmitted"
		Write-Host ("rtt min/avg/max = {0:F3} / {1:F3} / {2:F3} ms" -f $minRtt, $avgRtt, $maxRtt)
	}
	if ($null -ne $client) { $client.Close() }
}
