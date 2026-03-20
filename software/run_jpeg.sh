#!/bin/bash
# Complete JPEG MPSoC pipeline runner.
# - Builds the SDRAM input image from software/input.raw
#   or converts a standard image file into raw RGB first
# - Downloads input + all CPU ELFs
# - Lets the CPUs run undisturbed, then inspects result/progress words in SDRAM
# - Extracts the final JPEG into software/generated/output.jpg

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
GEN_DIR="$SCRIPT_DIR/generated"

RAW_INPUT="${RAW_INPUT:-$SCRIPT_DIR/input.raw}"
IMAGE_INPUT="${IMAGE_INPUT:-}"
SREC="${SREC:-$GEN_DIR/image_data.srec}"
OUTPUT="${OUTPUT:-$GEN_DIR/output.jpg}"
GDB_PORT="${GDB_PORT:-2343}"
INPUT_BASE="${INPUT_BASE:-0x02400000}"
JPEG_META_BASE="${JPEG_META_BASE:-0x02600000}"
CPU0_STATUS_ADDR="${CPU0_STATUS_ADDR:-0x02600020}"
CPU0_MCU_ADDR="${CPU0_MCU_ADDR:-0x02600024}"
JDI_FILE="${JDI_FILE:-$PROJECT_ROOT/output_files/toplevel.jdi}"
SOF_FILE="${SOF_FILE:-$PROJECT_ROOT/output_files/toplevel.sof}"

WIDTH="${WIDTH:-320}"
HEIGHT="${HEIGHT:-240}"
QUALITY="${QUALITY:-1024}"
IMAGE_FORMAT="${IMAGE_FORMAT:-4}"
OPT_LEVEL="${OPT_LEVEL:--O2}"
CPU2_USE_CUSTOM_DCT="${CPU2_USE_CUSTOM_DCT:-0}"
TIMEOUT="${TIMEOUT:-120}"
DUMP_TIMEOUT="${DUMP_TIMEOUT:-180}"
POLL_INTERVAL="${POLL_INTERVAL:-5}"
DEVICE_INDEX="${DEVICE_INDEX:-1}"
JTAG_DEVICE_INDEX="${JTAG_DEVICE_INDEX:-1}"
CABLE_FLAG=(--cable "$DEVICE_INDEX")
DEVICE_FLAG=(--device "$JTAG_DEVICE_INDEX")
SYSID_ID="${SYSID_ID:-0}"
SYSID_BASE="${SYSID_BASE:-0x4041090}"
SYSID_TIMESTAMP="${SYSID_TIMESTAMP:-}"
SYSID_SOURCE_FILE="${SYSID_SOURCE_FILE:-$SCRIPT_DIR/cpu_0_bsp/public.mk}"
SYSID_FLAGS=()
GDB_TARGET_CMD_CPU5_STOP=""
TERMINAL_PIDS=()
declare -a TERMINAL_LOG_LINES
ACTIVE_GDB_PORT=""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

find_python() {
    local candidate
    for candidate in \
        "/cygdrive/c/Users/dell/AppData/Local/Programs/Python/Python313/python.exe" \
        "/cygdrive/c/Users/dell/AppData/Local/Programs/Python/Python312/python.exe" \
        "/cygdrive/c/Users/dell/AppData/Local/Programs/Python/Python311/python.exe" \
        "/cygdrive/c/Python313/python.exe" \
        "/cygdrive/c/Python312/python.exe" \
        "/cygdrive/c/Python311/python.exe"
    do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return
        fi
    done
    if command -v py >/dev/null 2>&1; then
        echo "py -3"
        return
    fi
    if command -v python >/dev/null 2>&1 && [ -x "$(command -v python)" ]; then
        echo "python"
        return
    fi
    if command -v python3 >/dev/null 2>&1 && [ -x "$(command -v python3)" ]; then
        echo "python3"
        return
    fi
    return 1
}

to_host_path() {
    local path_value="$1"
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$path_value"
    else
        echo "$path_value"
    fi
}

to_host_gdb_path() {
    local path_value="$1"
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$path_value"
    else
        echo "$path_value"
    fi
}

require_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo -e "${RED}ERROR: required tool '$1' is not on PATH.${NC}"
        exit 1
    fi
}

load_sysid_flags() {
    local value
    local sysid_id="$SYSID_ID"
    local sysid_base="$SYSID_BASE"
    local sysid_timestamp="$SYSID_TIMESTAMP"

    if [ -f "$SYSID_SOURCE_FILE" ]; then
        value="$(sed -n 's/^SOPC_SYSID_FLAG += --id=\(.*\)$/\1/p' "$SYSID_SOURCE_FILE" | tail -n 1)"
        if [ -n "$value" ]; then
            sysid_id="$value"
        fi

        value="$(sed -n 's/^SOPC_SYSID_FLAG += --sidp=\(.*\)$/\1/p' "$SYSID_SOURCE_FILE" | tail -n 1)"
        if [ -n "$value" ]; then
            sysid_base="$value"
        fi

        value="$(sed -n 's/^SOPC_SYSID_FLAG += --timestamp=\([0-9][0-9]*\)$/\1/p' "$SYSID_SOURCE_FILE" | tail -n 1)"
        if [ -n "$value" ]; then
            sysid_timestamp="$value"
        fi
    fi

    if [ -z "$sysid_timestamp" ]; then
        echo -e "${RED}ERROR: could not determine the current SOPC system timestamp.${NC}"
        echo "Set SYSID_TIMESTAMP manually or regenerate $SYSID_SOURCE_FILE."
        exit 1
    fi

    SYSID_FLAGS=(--id="$sysid_id" --sidp="$sysid_base" --timestamp="$sysid_timestamp")
}

run_altera_tool() {
    set +u
    "$@"
    local rc=$?
    set -u
    return $rc
}

run_gdb_batch() {
    local script_path="$1"
    local elf_path="$SCRIPT_DIR/cpu_0/cpu_0.elf"
    script_path="$(to_host_path "$script_path")"
    elf_path="$(to_host_path "$elf_path")"
    run_altera_tool nios2-elf-gdb --batch --command="$script_path" "$elf_path"
}

run_gdb_batch_for_elf() {
    local script_path="$1"
    local elf_path="$2"
    script_path="$(to_host_path "$script_path")"
    elf_path="$(to_host_path "$elf_path")"
    run_altera_tool nios2-elf-gdb --batch --command="$script_path" "$elf_path"
}

stop_gdb_server() {
    if [ -n "${GDB_PID:-}" ]; then
        kill "$GDB_PID" 2>/dev/null || true
        wait "$GDB_PID" 2>/dev/null || true
        GDB_PID=""
    fi
    ACTIVE_GDB_PORT=""
}

start_gdb_server_tcp() {
    local cpu_idx="$1"
    local base_port="${2:-$GDB_PORT}"
    local server_log="$3"
    local try_port
    local try_idx

    stop_gdb_server

    for try_idx in 0 1 2 3 4; do
        try_port=$((base_port + try_idx))
        : > "$server_log"
        run_altera_tool nios2-gdb-server --cable "$DEVICE_INDEX" --device "$JTAG_DEVICE_INDEX" --instance "$cpu_idx" --tcpport "$try_port" >"$server_log" 2>&1 &
        GDB_PID="$!"
        sleep 1

        if kill -0 "$GDB_PID" 2>/dev/null; then
            ACTIVE_GDB_PORT="$try_port"
            return 0
        fi

        wait "$GDB_PID" 2>/dev/null || true
        GDB_PID=""
        ACTIVE_GDB_PORT=""
        if ! grep -q "Unable to bind" "$server_log" 2>/dev/null; then
            break
        fi
    done

    return 1
}

start_terminal_monitor() {
    local cpu_idx="$1"
    local log_file="$GEN_DIR/cpu_${cpu_idx}.terminal.log"
    : > "$log_file"
    TERMINAL_LOG_LINES[$cpu_idx]=0
    run_altera_tool nios2-terminal -c "$DEVICE_INDEX" --instance "$cpu_idx" > "$log_file" 2>&1 &
    TERMINAL_PIDS+=("$!")
}

flush_terminal_log() {
    local cpu_idx="$1"
    local log_file="$GEN_DIR/cpu_${cpu_idx}.terminal.log"
    local last_line="${TERMINAL_LOG_LINES[$cpu_idx]:-0}"
    local total_lines=0

    if [ ! -f "$log_file" ]; then
        return
    fi

    total_lines=$(wc -l < "$log_file")
    if [ "$total_lines" -gt "$last_line" ]; then
        sed -n "$((last_line + 1)),$((total_lines))p" "$log_file" | while IFS= read -r line; do
            case "$line" in
                JPEG_DUMP\ *)
                    ;;
                *)
                    printf '  [cpu%s] %s\n' "$cpu_idx" "$line"
                    ;;
            esac
        done
        TERMINAL_LOG_LINES[$cpu_idx]="$total_lines"
    fi
}

flush_terminal_logs() {
    local cpu_idx
    for cpu_idx in 0 1 2 3 4 5; do
        flush_terminal_log "$cpu_idx"
    done
}

terminal_log_contains() {
    local cpu_idx="$1"
    local pattern="$2"
    local log_file="$GEN_DIR/cpu_${cpu_idx}.terminal.log"
    grep -q "$pattern" "$log_file" 2>/dev/null
}

terminal_log_last_number() {
    local cpu_idx="$1"
    local pattern="$2"
    local log_file="$GEN_DIR/cpu_${cpu_idx}.terminal.log"
    sed -n "s/.*${pattern}\\([0-9][0-9]*\\).*/\\1/p" "$log_file" 2>/dev/null | tail -n 1
}

terminal_log_last_mcu_count() {
    local cpu_idx="$1"
    local log_file="$GEN_DIR/cpu_${cpu_idx}.terminal.log"
    sed -n 's/.*(\([0-9][0-9]*\)\/[0-9][0-9]* MCUs).*/\1/p' "$log_file" 2>/dev/null | tail -n 1
}

terminal_log_last_field() {
    local cpu_idx="$1"
    local field_name="$2"
    local log_file="$GEN_DIR/cpu_${cpu_idx}.terminal.log"
    sed -n "s/.*METRICS .*${field_name}=\\([0-9][0-9]*\\).*/\\1/p" "$log_file" 2>/dev/null | tail -n 1
}

format_rate_x100() {
    local count="${1:-0}"
    local elapsed_ms="${2:-0}"
    local rate_x100
    local whole
    local frac

    if [ -z "$count" ] || [ -z "$elapsed_ms" ] || [ "$elapsed_ms" -le 0 ]; then
        echo "n/a"
        return
    fi

    rate_x100=$(( (count * 100000) / elapsed_ms ))
    whole=$(( rate_x100 / 100 ))
    frac=$(( rate_x100 % 100 ))
    printf '%u.%02u' "$whole" "$frac"
}

stop_terminal_monitors() {
    local pid
    for pid in "${TERMINAL_PIDS[@]:-}"; do
        if [ -n "$pid" ]; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    TERMINAL_PIDS=()
}

read_stage_globals() {
    local cpu_idx="$1"
    local mode="${2:-poll}"
    local script_path="$GEN_DIR/read_cpu_${cpu_idx}_stage.gdb"
    local words_file="$GEN_DIR/read_cpu_${cpu_idx}_stage.words"
    local output
    local stop_flag=""

    if [ "$mode" = "stop" ]; then
        stop_flag=" --stop"
    fi

    cat > "$script_path" <<GDBEOF
target remote | nios2-gdb-server --cable $DEVICE_INDEX --device $JTAG_DEVICE_INDEX --instance $cpu_idx$stop_flag
x/wx &g_stage_status
x/wx &g_stage_count
quit
GDBEOF
    output="$(run_gdb_batch_for_elf "$script_path" "$SCRIPT_DIR/cpu_${cpu_idx}/cpu_${cpu_idx}.elf" 2>&1 || true)"
    printf '%s\n' "$output" | sed -n 's/.*:\s*0x\([0-9a-fA-F]\+\).*/0x\1/p' > "$words_file"
    mapfile -t STAGE_WORDS < "$words_file"
    printf '%s %s\n' "${STAGE_WORDS[0]:-0x0}" "${STAGE_WORDS[1]:-0x0}"
}

read_shared_state() {
    local script_path="$GEN_DIR/read_shared_state.gdb"
    local words_file="$GEN_DIR/read_shared_state.words"
    local output

    cat > "$script_path" <<GDBEOF
$GDB_TARGET_CMD_CPU5_STOP
x/wx $JPEG_META_BASE
x/wx $((JPEG_META_BASE + 4))
x/wx $((JPEG_META_BASE + 8))
x/wx $((JPEG_META_BASE + 12))
x/wx $CPU0_STATUS_ADDR
x/wx $CPU0_MCU_ADDR
quit
GDBEOF
    output="$(run_gdb_batch_for_elf "$script_path" "$SCRIPT_DIR/cpu_5/cpu_5.elf" 2>&1 || true)"
    printf '%s\n' "$output" | sed -n 's/.*:\s*0x\([0-9a-fA-F]\+\).*/0x\1/p' > "$words_file"
    mapfile -t SHARED_STATE_WORDS < "$words_file"
}

cleanup() {
    stop_terminal_monitors
    stop_gdb_server
}

trap cleanup EXIT

echo ""
echo -e "${CYAN}============================================${NC}"
echo -e "${CYAN}   JPEG MPSoC Pipeline Runner${NC}"
echo -e "${CYAN}============================================${NC}"
echo ""

PYTHON_BIN="$(find_python)" || {
    echo -e "${RED}ERROR: python is required to build the SDRAM SREC image.${NC}"
    exit 1
}

if [ -z "$IMAGE_INPUT" ] && [ ! -f "$RAW_INPUT" ]; then
    shopt -s nullglob
    ROOT_IMAGE_CANDIDATES=(
        "$PROJECT_ROOT"/*.jpg
        "$PROJECT_ROOT"/*.jpeg
        "$PROJECT_ROOT"/*.png
        "$PROJECT_ROOT"/*.bmp
    )
    shopt -u nullglob
    if [ "${#ROOT_IMAGE_CANDIDATES[@]}" -eq 1 ]; then
        IMAGE_INPUT="${ROOT_IMAGE_CANDIDATES[0]}"
    fi
fi

if [ -n "$IMAGE_INPUT" ]; then
    RAW_INPUT="$IMAGE_INPUT"
fi

case "${RAW_INPUT,,}" in
    *.jpg|*.jpeg|*.png|*.bmp)
        CONVERT_SCRIPT_PATH="$SCRIPT_DIR/convert_image_to_raw.py"
        CONVERTED_RAW="$GEN_DIR/input_from_image.raw"
        PY_CONVERT_SCRIPT="$CONVERT_SCRIPT_PATH"
        PY_CONVERT_INPUT="$RAW_INPUT"
        PY_CONVERT_OUTPUT="$CONVERTED_RAW"
        case "$PYTHON_BIN" in
            *python.exe*|py\ -3)
                PY_CONVERT_SCRIPT="$(to_host_path "$PY_CONVERT_SCRIPT")"
                PY_CONVERT_INPUT="$(to_host_path "$PY_CONVERT_INPUT")"
                PY_CONVERT_OUTPUT="$(to_host_path "$PY_CONVERT_OUTPUT")"
                ;;
        esac
        eval "$PYTHON_BIN" '"$PY_CONVERT_SCRIPT"' \
            --input '"$PY_CONVERT_INPUT"' \
            --output '"$PY_CONVERT_OUTPUT"' \
            --width "$WIDTH" \
            --height "$HEIGHT"
        RAW_INPUT="$CONVERTED_RAW"
        echo -e "${GREEN}  Converted image input to raw RGB: $(basename "$RAW_INPUT")${NC}"
        ;;
esac

PY_SCRIPT_PATH="$SCRIPT_DIR/build_image_srec.py"
PY_RAW_INPUT="$RAW_INPUT"
PY_SREC_OUTPUT="$SREC"

case "$PYTHON_BIN" in
    *python.exe*|py\ -3)
        PY_SCRIPT_PATH="$(to_host_path "$PY_SCRIPT_PATH")"
        PY_RAW_INPUT="$(to_host_path "$PY_RAW_INPUT")"
        PY_SREC_OUTPUT="$(to_host_path "$PY_SREC_OUTPUT")"
        ;;
esac

GDB_TARGET_CMD_CPU5_STOP="target remote | nios2-gdb-server --cable $DEVICE_INDEX --device $JTAG_DEVICE_INDEX --instance 5 --stop"

require_tool nios2-download
require_tool nios2-elf-gdb
require_tool nios2-gdb-server
require_tool nios2-configure-sof
require_tool nios2-terminal
require_tool make

mkdir -p "$GEN_DIR"
load_sysid_flags

echo -e "${YELLOW}[1/6] Building SDRAM image...${NC}"
echo "  Build settings: OPT_LEVEL=$OPT_LEVEL CPU2_USE_CUSTOM_DCT=$CPU2_USE_CUSTOM_DCT"
for cpu in 0 1 2 3 4 5; do
    echo "  Rebuilding cpu_${cpu}..."
    if [ "$cpu" -eq 0 ]; then
        (
            cd "$SCRIPT_DIR"
            make -C "cpu_${cpu}" clean all APP_MAIN=main_safe.c APP_CFLAGS_OPTIMIZATION="$OPT_LEVEL" > /dev/null
        )
    elif [ "$cpu" -eq 2 ]; then
        (
            cd "$SCRIPT_DIR"
            make -C "cpu_${cpu}" clean all APP_CFLAGS_OPTIMIZATION="$OPT_LEVEL" USE_CUSTOM_DCT="$CPU2_USE_CUSTOM_DCT" > /dev/null
        )
    else
        (
            cd "$SCRIPT_DIR"
            make -C "cpu_${cpu}" clean all APP_CFLAGS_OPTIMIZATION="$OPT_LEVEL" > /dev/null
        )
    fi
done
eval "$PYTHON_BIN" '"$PY_SCRIPT_PATH"' \
    --raw '"$PY_RAW_INPUT"' \
    --output '"$PY_SREC_OUTPUT"' \
    --width "$WIDTH" \
    --height "$HEIGHT" \
    --quality "$QUALITY" \
    --image-format "$IMAGE_FORMAT" \
    --base-addr "$INPUT_BASE"
echo -e "${GREEN}  OK: $(basename "$SREC")${NC}"
echo "  Parameter block: quality=$QUALITY format=$IMAGE_FORMAT width=$WIDTH height=$HEIGHT"
echo ""

echo -e "${YELLOW}[2/6] Checking build files...${NC}"
if [ ! -f "$JDI_FILE" ]; then
    echo -e "${RED}ERROR: missing JDI file: $JDI_FILE${NC}"
    exit 1
fi
echo -e "${GREEN}  OK: $(basename "$JDI_FILE")${NC}"
if [ ! -f "$SOF_FILE" ]; then
    echo -e "${RED}ERROR: missing SOF file: $SOF_FILE${NC}"
    exit 1
fi
echo -e "${GREEN}  OK: $(basename "$SOF_FILE")${NC}"
for cpu in 0 1 2 3 4 5; do
    elf="$SCRIPT_DIR/cpu_${cpu}/cpu_${cpu}.elf"
    if [ ! -f "$elf" ]; then
        echo -e "${RED}ERROR: missing $elf${NC}"
        exit 1
    fi
    echo -e "${GREEN}  OK: cpu_${cpu}.elf${NC}"
done
echo ""

echo -e "${YELLOW}[3/6] Programming FPGA...${NC}"
run_altera_tool nios2-configure-sof "${CABLE_FLAG[@]}" "${DEVICE_FLAG[@]}" "$SOF_FILE" > /dev/null
sleep 3
echo -e "${GREEN}  FPGA configured with $(basename "$SOF_FILE").${NC}"
echo ""

echo -e "${YELLOW}[4/6] Clearing previous output marker...${NC}"
ZERO_SCRIPT="$GEN_DIR/clear_output.gdb"
cat > "$ZERO_SCRIPT" <<GDBEOF
$GDB_TARGET_CMD_CPU5_STOP
set {unsigned int}$JPEG_META_BASE = 0
set {unsigned int}($JPEG_META_BASE + 4) = 0
set {unsigned int}($JPEG_META_BASE + 8) = 0
set {unsigned int}($JPEG_META_BASE + 12) = 0
set {unsigned int}$CPU0_STATUS_ADDR = 0
set {unsigned int}$CPU0_MCU_ADDR = 0
quit
GDBEOF
run_gdb_batch_for_elf "$ZERO_SCRIPT" "$SCRIPT_DIR/cpu_5/cpu_5.elf" > /dev/null 2>&1 || {
    echo -e "${RED}ERROR: failed to clear the previous JPEG size marker in SDRAM.${NC}"
    exit 1
}
echo -e "${GREEN}  Output marker reset.${NC}"
echo ""

echo -e "${YELLOW}[5/6] Downloading input image and starting CPUs...${NC}"
if ! run_altera_tool nios2-download --jdi "$JDI_FILE" --cpu_name cpu_0 --instance 0 "${CABLE_FLAG[@]}" "${DEVICE_FLAG[@]}" "${SYSID_FLAGS[@]}" "$SREC"; then
    echo -e "${RED}ERROR: failed to download the SDRAM input image to cpu_0.${NC}"
    exit 1
fi
echo -e "${GREEN}  SDRAM image downloaded and verified by nios2-download.${NC}"
echo "  cpu_0 will read the image header from SDRAM after it is started."
echo "  Starting JTAG UART monitors..."
for cpu in 0 1 2 3 4 5; do
    start_terminal_monitor "$cpu"
done
sleep 1
for cpu in 5 4 3 2 1 0; do
    printf "  Loading cpu_%d..." "$cpu"
    elf="$SCRIPT_DIR/cpu_${cpu}/cpu_${cpu}.elf"
    if [ "$cpu" -eq 0 ]; then
        if ! run_altera_tool nios2-download --go --jdi "$JDI_FILE" --cpu_name "cpu_${cpu}" --instance "$cpu" "${CABLE_FLAG[@]}" "${DEVICE_FLAG[@]}" "${SYSID_FLAGS[@]}" "$elf"; then
            echo ""
            echo -e "${RED}ERROR: failed to download/start cpu_${cpu}.${NC}"
            exit 1
        fi
    else
        if ! run_altera_tool nios2-download --go --jdi "$JDI_FILE" --cpu_name "cpu_${cpu}" --instance "$cpu" "${CABLE_FLAG[@]}" "${DEVICE_FLAG[@]}" "$elf"; then
            echo ""
            echo -e "${RED}ERROR: failed to download/start cpu_${cpu}.${NC}"
            exit 1
        fi
    fi
    if [ $? -ne 0 ]; then
        echo ""
        echo -e "${RED}ERROR: failed to download/start cpu_${cpu}.${NC}"
        exit 1
    fi
    echo -e " ${GREEN}started${NC}"
    sleep 1
done
echo ""

echo -e "${YELLOW}[6/6] Waiting for JPEG completion...${NC}"
ELAPSED=0
PIPELINE_DONE=0
while :; do
    flush_terminal_logs

    if terminal_log_contains 5 "ERROR"; then
        break
    fi

    if [ "$PIPELINE_DONE" -eq 0 ] && terminal_log_contains 5 "DONE size="; then
        PIPELINE_DONE=1
        echo "  JPEG stream complete, waiting for cpu_5 UART dump..."
    fi

    if [ "$PIPELINE_DONE" -eq 1 ] && terminal_log_contains 5 "JPEG_DUMP_END size="; then
        break
    fi

    if [ "$PIPELINE_DONE" -eq 0 ] && [ "$ELAPSED" -ge "$TIMEOUT" ]; then
        break
    fi

    if [ "$PIPELINE_DONE" -eq 1 ] && [ "$ELAPSED" -ge $((TIMEOUT + DUMP_TIMEOUT)) ]; then
        break
    fi

    sleep "$POLL_INTERVAL"
    ELAPSED=$((ELAPSED + POLL_INTERVAL))
    echo "  ... monitoring live stage logs (${ELAPSED}s elapsed) ..."
done
flush_terminal_logs
stop_terminal_monitors

echo "  Checking SDRAM result..."
read_shared_state

JPEG_SIZE_HEX="${SHARED_STATE_WORDS[0]:-0x0}"
JPEG_PROGRESS_HEX="${SHARED_STATE_WORDS[1]:-0x0}"
JPEG_STATUS_HEX="${SHARED_STATE_WORDS[2]:-0x0}"
JPEG_ERROR_HEX="${SHARED_STATE_WORDS[3]:-0x0}"
CPU0_STATUS_HEX="${SHARED_STATE_WORDS[4]:-0x0}"
CPU0_MCU_HEX="${SHARED_STATE_WORDS[5]:-0x0}"

read -r CPU1_STATUS_HEX CPU1_MCU_HEX <<< "$(read_stage_globals 1 stop)"
read -r CPU2_STATUS_HEX CPU2_MCU_HEX <<< "$(read_stage_globals 2 stop)"
read -r CPU3_STATUS_HEX CPU3_MCU_HEX <<< "$(read_stage_globals 3 stop)"
read -r CPU4_STATUS_HEX CPU4_MCU_HEX <<< "$(read_stage_globals 4 stop)"

JPEG_SIZE_DEC=$(( ${JPEG_SIZE_HEX:-0x0} ))
JPEG_PROGRESS_DEC=$(( ${JPEG_PROGRESS_HEX:-0x0} ))
JPEG_STATUS_DEC=$(( ${JPEG_STATUS_HEX:-0x0} ))
JPEG_ERROR_DEC=$(( ${JPEG_ERROR_HEX:-0x0} ))
CPU0_STATUS_DEC=$(( ${CPU0_STATUS_HEX:-0x0} ))
CPU0_MCU_DEC=$(( ${CPU0_MCU_HEX:-0x0} ))
CPU1_STATUS_DEC=$(( ${CPU1_STATUS_HEX:-0x0} ))
CPU1_MCU_DEC=$(( ${CPU1_MCU_HEX:-0x0} ))
CPU2_STATUS_DEC=$(( ${CPU2_STATUS_HEX:-0x0} ))
CPU2_MCU_DEC=$(( ${CPU2_MCU_HEX:-0x0} ))
CPU3_STATUS_DEC=$(( ${CPU3_STATUS_HEX:-0x0} ))
CPU3_MCU_DEC=$(( ${CPU3_MCU_HEX:-0x0} ))
CPU4_STATUS_DEC=$(( ${CPU4_STATUS_HEX:-0x0} ))
CPU4_MCU_DEC=$(( ${CPU4_MCU_HEX:-0x0} ))
TOTAL_MCUS=$(( ((WIDTH + 7) / 8) * ((HEIGHT + 7) / 8) ))

CPU5_DONE_SIZE_LOG="$(terminal_log_last_number 5 "DONE size=" || true)"
if [ -n "${CPU5_DONE_SIZE_LOG:-}" ] && [ "$JPEG_SIZE_DEC" -le 0 ]; then
    JPEG_SIZE_DEC="$CPU5_DONE_SIZE_LOG"
    JPEG_PROGRESS_DEC=100
    JPEG_STATUS_DEC=4
    JPEG_ERROR_DEC=0
fi

if terminal_log_contains 0 "finished streaming all"; then
    CPU0_STATUS_DEC=4
    if [ "$CPU0_MCU_DEC" -le 0 ]; then
        CPU0_MCU_DEC=$(( ((WIDTH + 7) / 8) * ((HEIGHT + 7) / 8) ))
    fi
fi
if terminal_log_contains 1 "finished"; then
    CPU1_STATUS_DEC=4
    if [ "$CPU1_MCU_DEC" -le 0 ]; then
        CPU1_MCU_DEC="$(terminal_log_last_mcu_count 1 || true)"
        CPU1_MCU_DEC="${CPU1_MCU_DEC:-0}"
    fi
fi
if terminal_log_contains 2 "finished"; then
    CPU2_STATUS_DEC=4
    if [ "$CPU2_MCU_DEC" -le 0 ]; then
        CPU2_MCU_DEC="$(terminal_log_last_mcu_count 2 || true)"
        CPU2_MCU_DEC="${CPU2_MCU_DEC:-0}"
    fi
fi
if terminal_log_contains 3 "finished"; then
    CPU3_STATUS_DEC=4
    if [ "$CPU3_MCU_DEC" -le 0 ]; then
        CPU3_MCU_DEC="$(terminal_log_last_mcu_count 3 || true)"
        CPU3_MCU_DEC="${CPU3_MCU_DEC:-0}"
    fi
fi
if terminal_log_contains 4 "finished"; then
    CPU4_STATUS_DEC=4
    if [ "$CPU4_MCU_DEC" -le 0 ]; then
        CPU4_MCU_DEC="$(terminal_log_last_mcu_count 4 || true)"
        CPU4_MCU_DEC="${CPU4_MCU_DEC:-0}"
    fi
fi

echo "  cpu0 status: ${CPU0_STATUS_DEC}, mcus sent: ${CPU0_MCU_DEC}"
echo "  cpu1 status: ${CPU1_STATUS_DEC}, mcus done: ${CPU1_MCU_DEC}"
echo "  cpu2 status: ${CPU2_STATUS_DEC}, mcus done: ${CPU2_MCU_DEC}"
echo "  cpu3 status: ${CPU3_STATUS_DEC}, mcus done: ${CPU3_MCU_DEC}"
echo "  cpu4 status: ${CPU4_STATUS_DEC}, mcus done: ${CPU4_MCU_DEC}"
echo "  cpu5 status: ${JPEG_STATUS_DEC}, progress: ${JPEG_PROGRESS_DEC}%, size: ${JPEG_SIZE_DEC}"

CPU0_ELAPSED_MS="$(terminal_log_last_field 0 elapsed_ms || true)"
CPU1_ELAPSED_MS="$(terminal_log_last_field 1 elapsed_ms || true)"
CPU2_ELAPSED_MS="$(terminal_log_last_field 2 elapsed_ms || true)"
CPU3_ELAPSED_MS="$(terminal_log_last_field 3 elapsed_ms || true)"
CPU4_ELAPSED_MS="$(terminal_log_last_field 4 elapsed_ms || true)"
CPU5_ELAPSED_MS="$(terminal_log_last_field 5 elapsed_ms || true)"
CPU5_BYTES_LOG="$(terminal_log_last_field 5 bytes || true)"
CPU5_MCUS_LOG="$(terminal_log_last_field 5 mcus || true)"

echo "  throughput cpu0: $(format_rate_x100 "${CPU0_MCU_DEC:-0}" "${CPU0_ELAPSED_MS:-0}") MCUs/s, elapsed ${CPU0_ELAPSED_MS:-n/a} ms"
echo "  throughput cpu1: $(format_rate_x100 "${CPU1_MCU_DEC:-0}" "${CPU1_ELAPSED_MS:-0}") MCUs/s, elapsed ${CPU1_ELAPSED_MS:-n/a} ms"
echo "  throughput cpu2: $(format_rate_x100 "${CPU2_MCU_DEC:-0}" "${CPU2_ELAPSED_MS:-0}") MCUs/s, elapsed ${CPU2_ELAPSED_MS:-n/a} ms"
echo "  throughput cpu3: $(format_rate_x100 "${CPU3_MCU_DEC:-0}" "${CPU3_ELAPSED_MS:-0}") MCUs/s, elapsed ${CPU3_ELAPSED_MS:-n/a} ms"
echo "  throughput cpu4: $(format_rate_x100 "${CPU4_MCU_DEC:-0}" "${CPU4_ELAPSED_MS:-0}") MCUs/s, elapsed ${CPU4_ELAPSED_MS:-n/a} ms"
echo "  throughput cpu5: $(format_rate_x100 "${CPU5_BYTES_LOG:-$JPEG_SIZE_DEC}" "${CPU5_ELAPSED_MS:-0}") bytes/s, $(format_rate_x100 "${CPU5_MCUS_LOG:-$TOTAL_MCUS}" "${CPU5_ELAPSED_MS:-0}") MCUs/s, elapsed ${CPU5_ELAPSED_MS:-n/a} ms"

if [ "$JPEG_ERROR_DEC" -ne 0 ] || [ "$JPEG_STATUS_DEC" -eq 5 ]; then
    echo -e "${RED}ERROR: CPU5 reported a pipeline/output error.${NC}"
    exit 1
fi

if [ "$JPEG_SIZE_DEC" -le 0 ]; then
    echo -e "${RED}ERROR: timed out waiting for CPU5 to publish the JPEG size.${NC}"
    echo "The pipeline may still be running, or one of the CPUs may be stalled."
    exit 1
fi

echo -e "${GREEN}  JPEG size: $JPEG_SIZE_DEC bytes${NC}"
echo ""

echo "Extracting JPEG from cpu_5 UART dump..."
UART_EXTRACT_SCRIPT="$SCRIPT_DIR/extract_jpeg_uart.py"
UART_EXTRACT_LOG="$GEN_DIR/extract_uart.log"
UART_INPUT_LOG="$GEN_DIR/cpu_5.terminal.log"
UART_SCRIPT_PATH="$UART_EXTRACT_SCRIPT"
UART_LOG_PATH="$UART_INPUT_LOG"
UART_OUTPUT_PATH="$OUTPUT"

case "$PYTHON_BIN" in
    *python.exe*|py\ -3)
        UART_SCRIPT_PATH="$(to_host_path "$UART_SCRIPT_PATH")"
        UART_LOG_PATH="$(to_host_path "$UART_LOG_PATH")"
        UART_OUTPUT_PATH="$(to_host_path "$UART_OUTPUT_PATH")"
        ;;
esac

rm -f "$OUTPUT" "$UART_EXTRACT_LOG"
if eval "$PYTHON_BIN" '"$UART_SCRIPT_PATH"' \
    --log '"$UART_LOG_PATH"' \
    --output '"$UART_OUTPUT_PATH"' \
    --expected-size "$JPEG_SIZE_DEC" >"$UART_EXTRACT_LOG" 2>&1; then
    EXTRACT_OK=1
else
    EXTRACT_OK=0
fi

echo ""
if [ "$EXTRACT_OK" -eq 1 ] && [ -f "$OUTPUT" ] && [ -s "$OUTPUT" ]; then
    if command -v xxd >/dev/null 2>&1; then
        START_SIG=$(xxd -p -l 2 "$OUTPUT" 2>/dev/null || true)
        END_SIG=$(tail -c 2 "$OUTPUT" | xxd -p 2>/dev/null || true)
        echo "  JPEG start: $START_SIG"
        echo "  JPEG end  : $END_SIG"
    fi
    FSIZE=$(wc -c < "$OUTPUT")
    echo -e "${CYAN}============================================${NC}"
    echo -e "${GREEN}  SUCCESS! JPEG saved.${NC}"
    echo -e "${CYAN}  File : $OUTPUT${NC}"
    echo -e "${CYAN}  Size : $FSIZE bytes${NC}"
    echo -e "${CYAN}============================================${NC}"
else
    echo -e "${RED}ERROR: extraction failed. See $UART_EXTRACT_LOG${NC}"
    if [ -f "$UART_EXTRACT_LOG" ]; then
        sed 's/^/  /' "$UART_EXTRACT_LOG"
    fi
    exit 1
fi
