# Writes a camdb.bin to the camdb partition (0x710000) on the Heltec over USB.
param([Parameter(Mandatory = $true)][string]$Bin, [string]$Port = "COM11")
$esptool = Join-Path $env:USERPROFILE ".platformio\packages\tool-esptoolpy\esptool.py"
$size = (Get-Item $Bin).Length
if ($size -gt 0x8F0000) { throw "camdb.bin is $size bytes, larger than the 0x8F0000 partition" }
python $esptool --chip esp32s3 --port $Port --baud 921600 --after watchdog_reset write_flash 0x710000 $Bin
