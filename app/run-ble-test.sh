#!/bin/sh

# Copyright (c) 2020 The ZMK Contributors
# SPDX-License-Identifier: MIT

if [ -z "$1" ]; then
    echo "Usage: ./run-ble-test.sh <path to testcase>"
    exit 1
fi

path="$1"
if [ $path = "all" ]; then
    path="tests"
fi

if ! [ -e "${BSIM_OUT_PATH}/bin/ble_test_central.exe" ]; then
    west build -d build/tests/ble/central -b nrf52_bsim tests/ble/central > /dev/null 2>&1 && \
        cp build/tests/ble/central/zephyr/zephyr.exe "${BSIM_OUT_PATH}/bin/ble_test_central.exe"
fi

testcases=$(find $path -name nrf52_bsim.keymap -exec dirname \{\} \;)
num_cases=$(echo "$testcases" | wc -l)
if [ $num_cases -gt 1 ] || [ "$testcases" != "$path" ]; then
    echo "" > ./build/tests/pass-fail.log
    echo "$testcases" | xargs -L 1 -P ${J:-4} ./run-ble-test.sh
    err=$?
    sort -k2 ./build/tests/pass-fail.log
    exit $err
fi

testcase="$path"
echo "Running $testcase:"

west build -d build/$testcase -b nrf52_bsim -- -DZMK_CONFIG="$(pwd)/$testcase" > /dev/null 2>&1
if [ $? -gt 0 ]; then
    echo "FAILED: $testcase did not build" | tee -a ./build/tests/pass-fail.log
    exit 1
fi

exe_name=${testcase//\//_}

start_dir=$(pwd)
cp build/$testcase/zephyr/zmk.exe "${BSIM_OUT_PATH}/bin/${exe_name}"
pushd "${BSIM_OUT_PATH}/bin"
./ble_test_central.exe -d=0 -s=${exe_name} | tee -a "${start_dir}/build/$testcase/output.log" &
./${exe_name} -d=1 -s=${exe_name} | tee -a "${start_dir}/build/$testcase/output.log" &
./bs_device_handbrake -s=${exe_name} -d=2 -r=10 &
./bs_2G4_phy_v1 -s=${exe_name} -D=3 -sim_length=50e6

popd

# ./build/$testcase/zephyr/zmk.exe | sed -e "s/.*> //" | tee build/$testcase/keycode_events_full.log | sed -n -f $testcase/events.patterns > build/$testcase/keycode_events.log
# diff -auZ $testcase/keycode_events.snapshot build/$testcase/keycode_events.log
# if [ $? -gt 0 ]; then
#     if [ -f $testcase/pending ]; then
#         echo "PENDING: $testcase" | tee -a ./build/tests/pass-fail.log
#         exit 0
#     fi
#     echo "FAILED: $testcase" | tee -a ./build/tests/pass-fail.log
#     exit 1
# fi

# echo "PASS: $testcase" | tee -a ./build/tests/pass-fail.log
# exit 0