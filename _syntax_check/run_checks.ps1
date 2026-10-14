# Verification suite for EnvMon_WIFI. ASCII only on purpose: PowerShell 5.1
# decodes BOM-less files as ANSI/GBK, which mangles Chinese comments and can
# break the whole block. Keep this file ASCII.
#
# Sections:
#   1. per-file syntax check (arm-none-eabi-gcc -fsyntax-only, warnings shown)
#   2. Hzk1 font table dimensions (compile-time asserts)
#   3. duplicate global symbols (the L6200E class of error)
#   4. OLED field widths fit 16 columns
#   5. glyph indices in OLED.h match the rows of the Hzk1 table
#   6. key driver behaviour on a host build (host gcc + printf)

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$armGcc = 'D:\Tool\arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-gcc.exe'
# Host compiler for section 6: the test binary runs on the PC and uses printf.
$hostGcc = 'D:\Tool\mingw64\bin\gcc.exe'

# NOTE: the stub directory comes FIRST on purpose. FreeRTOS's portmacro.h uses
# ARMCC-only inline assembly (__asm { msr basepri, ... }), which gcc cannot
# parse, so the FreeRTOS headers must be stubbed. Consequence: the stubs also
# shadow the real OLED.h / key.h etc. Keep each stub in sync with the real
# public interface -- a stale stub yields phantom errors (a fake OLED.h without
# the HZK_* enum once made task_oled.c look broken).
$inc = @('-I', '_syntax_check/stub', '-I', 'APP', '-I', 'BSP/OLED', '-I', 'BSP/dht11',
         '-I', 'BSP/ESP8266', '-I', 'BSP/ASR', '-I', 'BSP/KEY', '-I', 'BSP/BH1750',
         '-I', 'BSP/bsp_delay', '-I', 'Core/Inc')

$files = 'APP/app_ui.c', 'APP/task_oled.c', 'APP/task_wifi.c', 'APP/task_key.c',
         'APP/task_asr.c', 'APP/task_dht11.c', 'APP/task_light.c', 'APP/app_main.c',
         'BSP/OLED/OLED.c', 'BSP/KEY/key.c'

$bad = 0

'=== 1. syntax check per file (warnings included) ==='
foreach ($f in $files) {
    $o = & $armGcc -fsyntax-only -std=c99 -Wall -Wextra -Wno-unused-parameter @inc $f 2>&1 |
         Where-Object { $_ -match 'warning:|error:' }
    if ($o) {
        $bad++
        "  ISSUE $f"
        $o | Select-Object -First 6 | ForEach-Object { "        $_" }
    } else {
        "  OK    $f"
    }
}

''
'=== 2. Hzk1 table dimensions ==='
$o = & $armGcc -fsyntax-only -std=c99 -Wall -I BSP/OLED '_syntax_check/check_font.c' 2>&1 |
     Where-Object { $_ -match 'error:' }
if ($o) { $bad++; '  FAIL'; $o | Select-Object -First 6 } else { '  OK    Hzk1[30][32] / OLED_F8x16[95][16]' }

''
'=== 3. duplicate global symbols (L6200E class) ==='
python "$PSScriptRoot/check_dupes.py" | Select-Object -Last 3
if ($LASTEXITCODE -ne 0) { $bad++ }

''
'=== 4. OLED field widths ==='
python "$PSScriptRoot/check_layout.py" | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { $bad++ }

''
'=== 5. glyph indices match the Hzk1 table (names vs rows) ==='
python "$PSScriptRoot/check_glyph_index.py" | Select-Object -Last 1
if ($LASTEXITCODE -ne 0) { $bad++ }

''
'=== 6. key driver behaviour (host build) ==='
if (Test-Path $hostGcc) {
    $exe = "$PSScriptRoot/key_test.exe"
    $buildOut = & $hostGcc -std=c99 -Wall -Wextra `
        -I "$PSScriptRoot/stub" -I "$PSScriptRoot/../BSP/KEY" `
        -o $exe "$PSScriptRoot/key_test.c" "$PSScriptRoot/../BSP/KEY/key.c" 2>&1
    $buildOut | Where-Object { $_ -match 'error|warning' } | Select-Object -First 5
    if (Test-Path $exe) {
        & $exe | Select-Object -Last 1
        if ($LASTEXITCODE -ne 0) { $bad++ }
        Remove-Item $exe -Force -ErrorAction SilentlyContinue
    } else {
        '  build FAILED, skipped'
        $bad++
    }
} else {
    "  host gcc not found ($hostGcc), skipped"
}

''
"TOTAL PROBLEMS: $bad"
exit $bad
