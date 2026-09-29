#!/bin/sh

WIFI_INTERFACE="wlan0"
WPA_SOCK_DIR="/var/run/wpa_supplicant"
WPA_CONF_DIR="/mnt/sdcard/.minime/config/wifi"
WPA_CONF_FILE="${WPA_CONF_DIR}/wpa_supplicant.conf"

start() {
	if command -v rfkill.elf >/dev/null 2>&1; then
		rfkill.elf unblock wifi 2>/dev/null || true
	elif command -v rfkill >/dev/null 2>&1; then
		rfkill unblock wifi 2>/dev/null || true
	fi

	mkdir -p "${WPA_CONF_DIR}"
	echo 1 > "${WPA_CONF_DIR}/enabled"

	mkdir -p "${WPA_SOCK_DIR}"
	chmod 755 "${WPA_SOCK_DIR}"

	if [ ! -f "$WPA_CONF_FILE" ]; then
		cat > "$WPA_CONF_FILE" << 'EOF'
ctrl_interface=/var/run/wpa_supplicant
update_config=1
disable_scan_offload=1
wowlan_triggers=any

EOF
	fi

	if ! pgrep -x wpa_supplicant >/dev/null 2>&1; then
		wpa_supplicant -B -i "$WIFI_INTERFACE" -c "$WPA_CONF_FILE" -C "$WPA_SOCK_DIR"
	fi

	if ! pgrep -f "udhcpc.*$WIFI_INTERFACE" >/dev/null 2>&1; then
		udhcpc -i "$WIFI_INTERFACE" -b 2>/dev/null
	fi
}

stop() {
	rm -f "${WPA_CONF_DIR}/enabled"
	killall udhcpc 2>/dev/null || true
	killall wpa_supplicant 2>/dev/null || true

	if command -v rfkill.elf >/dev/null 2>&1; then
		rfkill.elf block wifi 2>/dev/null || true
	elif command -v rfkill >/dev/null 2>&1; then
		rfkill block wifi 2>/dev/null || true
	fi

	ip link set "$WIFI_INTERFACE" down 2>/dev/null || true
}

case "$1" in
  start|"") start ;;
  stop) stop ;;
  *) echo "Usage: $0 {start|stop}"; exit 1 ;;
esac