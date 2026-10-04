#!/bin/sh
# RK3326 hardware probing extension for minime

echo "Minime Boot Stage 1: Running RK3326 hardware autodetection..."

# Check if we are on RK3326 platform by scanning for RK3326 DTBs
if ls /mnt/card/.minime/devices/rk3326-* >/dev/null 2>&1; then
	if ls /sys/bus/sdio/devices/* >/dev/null 2>&1; then
		echo "device=rk3326-anbernic-rg351v.dtb" > /mnt/card/.minime/config/device.cfg
	elif grep -q "0bda" /sys/bus/usb/devices/*/idVendor 2>/dev/null && grep -q "b720" /sys/bus/usb/devices/*/idProduct 2>/dev/null; then
		echo "device=rk3326-anbernic-rg351m.dtb" > /mnt/card/.minime/config/device.cfg
	elif grep -q "1209" /sys/bus/usb/devices/*/idVendor 2>/dev/null; then
		echo "device=rk3326-anbernic-rg351p.dtb" > /mnt/card/.minime/config/device.cfg
	else
		echo "device=rk3326-anbernic-rg351mp.dtb" > /mnt/card/.minime/config/device.cfg
	fi
fi
