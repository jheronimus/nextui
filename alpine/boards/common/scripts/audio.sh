#!/bin/sh
# shellcheck shell=sh
# audio.sh: ALSA audio ownership (single source of truth for .asoundrc and mixer routing).
# The firmware owns ALSA routing; the UI delegates here instead of managing
# .asoundrc or mixer controls directly.
#
# Interfaces:
#   speakers    Onboard speakers (unmutes Internal Speakers / Speaker Amp, sets SPK path)
#   headphones  Wired 3.5mm jack (mutes Internal Speakers / Speaker Amp, sets HP path)
#   bluetooth   Bluetooth A2DP sink (generates BlueALSA /run/asoundrc route)
#   hdmi        HDMI audio sink (generates HDMI /run/asoundrc route)
#
# Commands:
#   start-interface <interface> [args]  Start interface and stop all other interfaces
#   stop-interface  <interface>         Stop the given interface
#
# Legacy / convenience aliases:
#   init                                Reset to default speakers interface
#   bt-on <addr>                        Alias for start-interface bluetooth <addr>
#   bt-off                              Alias for stop-interface bluetooth
#   jack-in                             Alias for start-interface headphones
#   jack-out                            Alias for start-interface speakers

set -eu

INTERFACES="speakers headphones bluetooth hdmi"
ASOUNDRC_FILE="/mnt/sdcard/.asoundrc"
RUN_ASOUNDRC="/run/asoundrc"
TRAITS_FILE="/mnt/sdcard/.minime/traits"

get_trait() {
	key="$1"
	[ -f "$TRAITS_FILE" ] || return 0
	grep "^${key}=" "$TRAITS_FILE" 2>/dev/null | tail -n 1 | cut -d= -f2 | tr -d '\r' || true
}

get_codec_card() {
	audio_card=$(get_trait audio_card)
	if [ -n "$audio_card" ] && [ "$audio_card" != "default" ]; then
		echo "$audio_card"
		return
	fi
	if [ -f /proc/asound/cards ]; then
		card=$(grep -v -i hdmi /proc/asound/cards 2>/dev/null | grep -o '\[.*\]' | head -n 1 | tr -d '[] ' || true)
		if [ -n "$card" ]; then
			echo "$card"
			return
		fi
	fi
	echo "default"
}

run_amixer() {
	card="$(get_codec_card)"
	if [ -n "$card" ] && [ "$card" != "default" ]; then
		amixer -q -c "$card" "$@" 2>/dev/null || true
	else
		amixer -q "$@" 2>/dev/null || true
	fi
}

get_connected_bt_sink() {
	if command -v bluetoothctl >/dev/null 2>&1; then
		bluetoothctl devices Connected 2>/dev/null | awk '/Device/ { print $2; exit }' || true
	fi
}

get_hdmi_card() {
	card=$(get_trait audio_hdmi_card)
	if [ -n "$card" ]; then
		echo "$card"
		return
	fi
	if [ -f /proc/asound/cards ]; then
		grep -i hdmi /proc/asound/cards 2>/dev/null | grep -o '\[.*\]' | head -n 1 | tr -d '[] ' || true
	fi
}

stop_interface() {
	stop_target="$1"
	case "$stop_target" in
	speakers)
		run_amixer sset 'Internal Speakers' off
		run_amixer sset 'Internal Speakers' mute
		run_amixer sset 'Speaker' mute
		run_amixer sset 'Speaker Amp' mute
		;;
	headphones)
		run_amixer sset 'Headphone' mute
		;;
	bluetooth)
		if grep -q "bluealsa" "$RUN_ASOUNDRC" 2>/dev/null; then
			rm -f "$RUN_ASOUNDRC" "$ASOUNDRC_FILE" 2>/dev/null || true
		fi
		;;
	hdmi)
		if grep -q "hw:.*" "$RUN_ASOUNDRC" 2>/dev/null; then
			rm -f "$RUN_ASOUNDRC" "$ASOUNDRC_FILE" 2>/dev/null || true
		fi
		;;
	*)
		echo "audio.sh: unknown interface '$stop_target' (expected: $INTERFACES)" >&2
		return 1
		;;
	esac
}

start_interface() {
	start_target="$1"
	shift || true

	# Stop all other interfaces first (mutual exclusivity)
	for other in $INTERFACES; do
		if [ "$other" != "$start_target" ]; then
			stop_interface "$other"
		fi
	done

	case "$start_target" in
	speakers)
		rm -f "$RUN_ASOUNDRC" "$ASOUNDRC_FILE" 2>/dev/null || true
		run_amixer sset 'Internal Speakers' on
		run_amixer sset 'Internal Speakers' unmute
		run_amixer sset 'Speaker' unmute
		run_amixer sset 'Speaker Amp' unmute
		run_amixer sset 'Playback Path' SPK
		run_amixer sset 'Playback Mux' HP
		;;
	headphones)
		rm -f "$RUN_ASOUNDRC" "$ASOUNDRC_FILE" 2>/dev/null || true
		run_amixer sset 'Internal Speakers' off
		run_amixer sset 'Internal Speakers' mute
		run_amixer sset 'Speaker Amp' mute
		run_amixer sset 'Headphone' unmute
		run_amixer sset 'Playback Path' HP_NO_MIC
		;;
	bluetooth)
		addr="${1:-}"
		if [ -z "$addr" ]; then
			addr="$(get_connected_bt_sink)"
		fi
		if [ -z "$addr" ]; then
			echo "audio.sh: error: no Bluetooth audio device specified or connected" >&2
			return 1
		fi
		if grep -q "device \"$addr\"" "$RUN_ASOUNDRC" 2>/dev/null; then
			return 0
		fi
		content=$(printf 'defaults.bluealsa.device "%s"\ndefaults.bluealsa.profile "a2dp"\npcm.!default {\n    type plug\n    slave {\n        pcm {\n            type bluealsa\n            device "%s"\n            profile "a2dp"\n        }\n        rate 48000\n    }\n}\nctl.!default {\n    type bluealsa\n}\n' \
			"$addr" "$addr")
		printf '%s' "$content" >"$RUN_ASOUNDRC"
		;;
	hdmi)
		hdmi_card="$(get_hdmi_card)"
		if [ -z "$hdmi_card" ]; then
			echo "audio.sh: error: no HDMI audio card detected" >&2
			return 1
		fi
		content=$(printf 'pcm.!default {\n    type plug\n    slave.pcm "hw:%s,0"\n}\nctl.!default {\n    type hw\n    card "%s"\n}\n' \
			"$hdmi_card" "$hdmi_card")
		printf '%s' "$content" >"$RUN_ASOUNDRC"
		;;
	*)
		echo "audio.sh: unknown interface '$start_target' (expected: $INTERFACES)" >&2
		return 1
		;;
	esac
}

usage() {
	echo "Usage: ${0##*/} {start-interface <interface> [args]|stop-interface <interface>}" >&2
	echo "Interfaces: $INTERFACES" >&2
	exit 1
}

case "${1:-}" in
start-interface)
	[ $# -ge 2 ] || usage
	shift
	start_interface "$@"
	;;
stop-interface)
	[ $# -ge 2 ] || usage
	stop_interface "$2"
	;;
init)
	start_interface speakers
	;;
bt-on)
	[ $# -ge 2 ] || usage
	start_interface bluetooth "$2"
	;;
bt-off)
	stop_interface bluetooth
	;;
jack-in)
	start_interface headphones
	;;
jack-out)
	start_interface speakers
	;;
*)
	usage
	;;
esac
