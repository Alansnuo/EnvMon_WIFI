# 编码探针: 只打印中文, 不写任何文件
$s = '校验 = 前 5 个字节逐个异或 (XOR) —— 对齐移植笔记 04 篇'
Write-Output "PROBE: $s"
Write-Output ("PROBE_LEN: " + $s.Length)
