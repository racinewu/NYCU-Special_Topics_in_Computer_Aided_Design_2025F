#!/bin/bash

TESTCASE_DIR="testcase"
TARGET="./bin/sop"
CHECKER="python3 2025_CAD_HW1/checker.py"

# Colors
BLUE="\e[34m"
PURPLE="\e[35m"
GREEN="\e[32m"
RED="\e[31m"
YELLOW="\e[33m"
RESET="\e[0m"

# Logging functions
log_info()    { printf "[${BLUE}INFO${RESET}] %s\n" "$*"; }
log_error()   { printf "[${RED}ERROR${RESET}] %s\n" "$*"; }
log_pass()    { printf "%-12s [%bPASS%b]\n" "$1" "$GREEN" "$RESET"; }
log_fail()    { printf "[${YELLOW}FAIL${RESET}] %s\n" "$*"; }
log_missing() { printf "[${PURPLE}MISSING${RESET} %s]\n" "$1"; }

usage() {
    printf "Usage: ./run.sh <case|all> [check|clean|valgrind]\n"
    exit 1
}

run_case() {
    local CASE=$1
    local MODE=$2
    local INPUT_FILE="$TESTCASE_DIR/${CASE}.txt"
    local OUTPUT_FILE="$TESTCASE_DIR/${CASE}.sop"

    if [[ ! -f "$INPUT_FILE" ]]; then
        printf "\n"
        log_error "$CASE.txt: No such file"
        return
    fi
    printf "\n"
    log_info "Running case:  $CASE..."
    if [[ "$MODE" == "valgrind" ]]; then
        valgrind --leak-check=full --show-leak-kinds=all "$TARGET" "$INPUT_FILE" "$OUTPUT_FILE"
    else
       TIME_OUTPUT=$( { /usr/bin/time -f "Real: %e s, User: %U s, Sys: %S s, CPU%%: %P, MaxMem: %M KB, VolCS: %c" timeout 180s "$TARGET" "$INPUT_FILE" "$OUTPUT_FILE"; } 2>&1 )
EXIT_CODE=$?
        if [ $EXIT_CODE -eq 124 ]; then
            log_fail "Timeout for 10s\n"
        else
            printf "%s\n" "$TIME_OUTPUT"
        fi
    fi
    log_info "Finished case: $CASE."
}

check_case() {
    local CASE=$1
    local INPUT_FILE="$TESTCASE_DIR/${CASE}.txt"
    local SOP_FILE="$TESTCASE_DIR/${CASE}.sop"

    printf "\n"
    log_info "Checking case: $CASE..."

    if [[ ! -f "$SOP_FILE" ]]; then
        log_missing "sop"
        return
    fi

    $CHECKER "$INPUT_FILE" "$SOP_FILE"

    log_info "Finished check for case: $CASE."
}

clean_case() {
    local CASE=$1
    printf "\n"

    if [[ "$CASE" == "all" ]]; then
        log_info "Cleaning all .sop files..."
        rm -f "$TESTCASE_DIR"/*.sop
        log_info "All .sop files cleaned."
    else
        infile="$TESTCASE_DIR/${CASE}.txt"
        if [[ ! -f "$infile" ]]; then
            log_error "$CASE.txt: No such case"
            return
        fi

        outfile="$TESTCASE_DIR/${CASE}.sop"
        if [[ -f "$outfile" ]]; then
            log_info "Cleaning case: $CASE..."
            rm -f "$outfile"
            log_info "Finished cleaning $CASE."
        else
            log_info "$CASE: nothing to clean."
        fi
    fi
}

# Check target existence unless cleaning
if [[ "$2" != "clean" && ! -x "$TARGET" ]]; then
    printf "\n"
    log_error "$TARGET not found or not executable!"
    printf "Please build it first (e.g. make).\n"
    exit 1
fi

# Parameter check
if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
fi

CASE=$1
MODE=$2

if [[ "$MODE" == "clean" ]]; then
    clean_case "$CASE"
    exit 0
fi

if [[ "$CASE" == "all" ]]; then
    for input_file in "$TESTCASE_DIR"/case*.txt; do
        casename=$(basename "$input_file" .txt)
        if [[ "$MODE" == "check" ]]; then
            check_case "$casename"
        else
            run_case "$casename" "$MODE"
        fi
    done
    printf "\n"
    log_info "Finished all cases."
else
    if [[ "$MODE" == "check" ]]; then
        check_case "$CASE"
    else
        run_case "$CASE" "$MODE"
    fi
fi