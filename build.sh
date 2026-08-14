#!/bin/bash

set -x -e -u -o pipefail

if [ -z "${SDK_PATH_PREFIX:-}" ]; then
    SDK_PATH_PREFIX=./sdk
fi

if [ -z "${BUILD_PATH_PREFIX:-}" ]; then
    BUILD_PATH_PREFIX=./build
fi

if [ -z "${ARTIFACTS_PATH:-}" ]; then
    ARTIFACTS_PATH=./artifacts
fi

mkdir -p "${BUILD_PATH_PREFIX}"

mkdir -p "${ARTIFACTS_PATH}"

mkdir -p "${SDK_PATH_PREFIX}"/zephyr/manifest
cp west.yml "${SDK_PATH_PREFIX}"/zephyr/manifest/west.yml
west init -l "${SDK_PATH_PREFIX}"/zephyr/manifest

export ZEPHYR_BASE=`realpath "${SDK_PATH_PREFIX}"/zephyr/zephyr`

west update -o=--depth=1 -n
west blobs fetch hal_espressif
west blobs fetch hal_infineon

west build -p -d "${BUILD_PATH_PREFIX}"/pico_2w -b rpi_pico2/rp2350a/m33/w app
cp "${BUILD_PATH_PREFIX}"/pico_2w/zephyr/zephyr.uf2 "${ARTIFACTS_PATH}"/rpc-pico2w.uf2

west build -p -d "${BUILD_PATH_PREFIX}"/xiao_esp32s3 -b xiao_esp32s3/esp32s3/procpu app
cp "${BUILD_PATH_PREFIX}"/xiao_esp32s3/zephyr/zephyr.bin "${ARTIFACTS_PATH}"/rpc-xiao_esp32s3.bin
