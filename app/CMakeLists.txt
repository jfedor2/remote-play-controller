cmake_minimum_required(VERSION 3.20.0)

set(EXTRA_DTC_OVERLAY_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/hid_dev.overlay
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ps5_remote_play_controller)

target_compile_definitions(app PRIVATE
    CHIAKI_LIB_ENABLE_MBEDTLS
    __ZEPHYR__
)

target_include_directories(app PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src
)

target_sources(app PRIVATE
    src/main.c
    src/wifi_manager.c
    src/settings.c
    src/registration.c
    src/session.c
    src/status.c
    src/led_status.c
    src/buttons.c
    src/chiaki_log.c

    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/regist.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/rpcrypt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/http.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/base64.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/common.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/log.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/random.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/stoppipe.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/sock.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/time.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/thread.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/remote/rudp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/remote/rudpsendbuffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/session.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/ecdh.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/gkcrypt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/ctrl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/takion.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/takionsendbuffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/streamconnection.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/controller.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/senkusha.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/congestioncontrol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/reorderqueue.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/launchspec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/packetstats.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/feedbacksender.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/audio.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/audioreceiver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/audiosender.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/bitstream.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/feedback.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/orientation.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/videoreceiver.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/src/frameprocessor.c
)

zephyr_ld_options(-Wl,--allow-multiple-definition)

include(${ZEPHYR_BASE}/modules/nanopb/nanopb.cmake)
zephyr_nanopb_sources(app ${CMAKE_CURRENT_SOURCE_DIR}/../chiaki-ng/lib/protobuf/takion.proto)
