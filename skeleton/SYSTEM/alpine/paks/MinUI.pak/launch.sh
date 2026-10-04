#!/bin/sh
# MinUI.pak - Alpine platform launcher

export PLATFORM="alpine"
if [ -d "/mnt/sdcard" ]; then
	export SDCARD_PATH="/mnt/sdcard"
else
	export SDCARD_PATH="/mnt/SDCARD"
fi

export BIOS_PATH="$SDCARD_PATH/Bios"
export ROMS_PATH="$SDCARD_PATH/Roms"
export SAVES_PATH="$SDCARD_PATH/Saves"
export CHEATS_PATH="$SDCARD_PATH/Cheats"
export SYSTEM_PATH="$SDCARD_PATH/.system/$PLATFORM"
export CORES_PATH="$SYSTEM_PATH/cores"
export USERDATA_PATH="$SDCARD_PATH/.userdata/$PLATFORM"
export SHARED_USERDATA_PATH="$SDCARD_PATH/.userdata/shared"
export LOGS_PATH="$USERDATA_PATH/logs"
export HOOKS_PATH="$USERDATA_PATH/.hooks"
export DATETIME_PATH="$SHARED_USERDATA_PATH/datetime.txt"
export HOME="$USERDATA_PATH"

#######################################

if [ -f "/tmp/poweroff" ]; then
	poweroff
	exit 0
fi
if [ -f "/tmp/reboot" ]; then
	reboot
	exit 0
fi

#######################################

mkdir -p "$BIOS_PATH"
mkdir -p "$ROMS_PATH"
mkdir -p "$SAVES_PATH"
mkdir -p "$CHEATS_PATH"
mkdir -p "$USERDATA_PATH"
mkdir -p "$LOGS_PATH"
mkdir -p "$HOOKS_PATH"
mkdir -p "$SHARED_USERDATA_PATH/.minui"

export IS_NEXT="yes"

# clear shadercache unconditionally
rm -rf "$SDCARD_PATH/.shadercache"

#######################################

export LD_LIBRARY_PATH="$SYSTEM_PATH/lib:/usr/lib:/lib:$LD_LIBRARY_PATH"
export PATH="$SYSTEM_PATH/bin:/usr/bin:/bin:$PATH"

amixer -q sset 'DAC' 100% unmute >/dev/null 2>&1 || true

if [ -x "$SYSTEM_PATH/bin/governor.sh" ]; then
	sh "$SYSTEM_PATH/bin/governor.sh" "auto"
fi

keymon.elf &
batmon.elf &

#######################################

AUTO_PATH="$USERDATA_PATH/auto.sh"
if [ -f "$AUTO_PATH" ]; then
	"$AUTO_PATH"
fi

# Composable boot hooks
if [ -x "$SYSTEM_PATH/bin/run_hooks.sh" ]; then
	"$SYSTEM_PATH/bin/run_hooks.sh" boot.d
fi

cd "$(dirname "$0")"

#######################################
# Hook system

parse_hook_cmd() {
	HOOK_CMD="$1"
	HOOK_EMU_PATH=$(echo "$HOOK_CMD" | sed "s/^'\\([^']*\\)'.*/\\1/")
	_remainder=$(echo "$HOOK_CMD" | sed "s/^'[^']*'//")
	if echo "$_remainder" | grep -q "'"; then
		HOOK_TYPE="rom"
		HOOK_ROM_PATH=$(echo "$_remainder" | sed "s/.*'\\([^']*\\)'.*/\\1/")
	else
		HOOK_TYPE="pak"
		HOOK_ROM_PATH=""
	fi
	[ -f /tmp/last.txt ] && HOOK_LAST=$(cat /tmp/last.txt) || HOOK_LAST=""
	export HOOK_CMD HOOK_EMU_PATH HOOK_TYPE HOOK_ROM_PATH HOOK_LAST
}

#######################################

EXEC_PATH="/tmp/nextui_exec"
NEXT_PATH="/tmp/next"
touch "$EXEC_PATH" && sync

killall -9 bootsplash 2>/dev/null || true
rm -f /run/bootsplash.pid

while [ -f "$EXEC_PATH" ]; do
	nextui.elf &> "$LOGS_PATH/nextui.txt"

	if [ -x "$SYSTEM_PATH/bin/governor.sh" ]; then
		sh "$SYSTEM_PATH/bin/governor.sh" "performance"
	fi

	if [ -f "$NEXT_PATH" ]; then
		CMD=$(cat "$NEXT_PATH")
		parse_hook_cmd "$CMD"
		if [ -x "$SYSTEM_PATH/bin/run_hooks.sh" ]; then
			"$SYSTEM_PATH/bin/run_hooks.sh" pre-launch.d
		fi
		eval "$CMD"
		if [ -x "$SYSTEM_PATH/bin/run_hooks.sh" ]; then
			"$SYSTEM_PATH/bin/run_hooks.sh" post-launch.d
		fi
		rm -f "$NEXT_PATH"
		if [ -x "$SYSTEM_PATH/bin/governor.sh" ]; then
			sh "$SYSTEM_PATH/bin/governor.sh" "performance"
		fi
	fi

	if [ -f "/tmp/poweroff" ]; then
		poweroff
		exit 0
	fi
	if [ -f "/tmp/reboot" ]; then
		reboot
		exit 0
	fi
done

poweroff
