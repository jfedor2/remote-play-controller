# PS5 Remote Play Controller

This repository contains code for game controller firmware that connects to a PS5 using the Remote Play protocol. The controller talks to the console directly over wifi, a PC is only required for the initial setup. This sidesteps the controller authentication issue you normally have to deal with on DIY controllers.

It uses a modified version of the [chiaki-ng](https://github.com/streetpea/chiaki-ng) project.

Pre-built binaries are provided for the Raspberry Pi Pico 2W and the Seeed Studio Xiao ESP32-S3.

_Please note this is a proof of concept. If you try to use it as your daily driver, you will probably run into issues._

Currently the code only supports controller buttons and not analog sticks. Triggers are supported, but on/off only, not analog.

Currently the controller only sends inputs over Remote Play. It does not work as a USB or Bluetooth controller.

Currently there's no sleep/wake function, so if you want your controller to be battery-powered, include some kind of an on/off switch.

## Flashing the firmware

You can find the pre-built binaries in the [latest release](https://github.com/jfedor2/remote-play-controller/releases/latest/).

If you're using the Raspberry Pi Pico 2W board, first put the board in firmware flashing mode by holding the BOOTSEL button on the board while you connect it to your PC. A drive named "RP2350" should appear. Copy the [rpc-pico2w.uf2](https://github.com/jfedor2/remote-play-controller/releases/latest/download/rpc-pico2w.uf2) file to that drive. After it finishes copying, the board is ready to use.

If you're using the Xiao ESP32-S3 board, you will need the [esptool](https://github.com/espressif/esptool) utility. First download the [rpc-xiao\_esp32s3.bin](https://github.com/jfedor2/remote-play-controller/releases/latest/download/rpc-xiao_esp32s3.bin) file. Then put your board in firmware flashing mode by holding the "B" button and, while holding it, pressing the "R" button. Then flash the firmware file with a command like this (adjust the serial port):

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash 0x0 rpc-xiao_esp32s3.bin
```

## Wiring

Wire the buttons to GPIO pins on your board as follows. On the Pico 2W you can additionally wire a status indicator LED to GPIO15. On the Xiao the onboard LED is used.

<details>
<summary>Raspberry Pi Pico 2W</summary>

pin | button
--- | ------
GPIO6 | cross
GPIO7 | circle
GPIO10 | square
GPIO11 | triangle
GPIO5 | D-pad left
GPIO4 | D-pad right
GPIO2 | D-pad up
GPIO3 | D-pad down
GPIO13 | L1
GPIO12 | R1
GPIO9 | L2
GPIO8 | R2
GPIO18 | L3
GPIO19 | R3
GPIO16 | create
GPIO17 | options
GPIO20 | PS
GPIO21 | touchpad click
</details>

<details>
<summary>Seeed Xiao ESP32-S3</summary>

pin | button
--- | ------
D0 | options
D1 | create
D2 | PS
D3 | cross
D4 | circle
D5 | square
D6 | triangle
D7 | D-pad left
D8 | D-pad right
D9 | D-pad up
D10 | D-pad down
</details>

## Setup

To connect the controller to your PS5, first you have to set it up using a computer. After that initial setup the computer is no longer required, the controller talks to the console directly over wifi.

Go to the [configuration website](https://www.jfedor.org/rpc-config/) using the desktop version of Chrome or another Chrome-based browser (it has to support WebHID). Once there, click "Connect to controller" to let the website talk to your controller, then provide the required information: wifi network name and password, PSN account ID and your PS5's IP address. Click "Save to device". Your controller should now be able to connect to your wifi. Then go to the "Pair" tab on the website and type in the PIN that is displayed on your PS5 and click "Pair". If all goes well the controller should be able to establish a Remote Play session to your console and it should be able to do that the next time on its own (the credentials are saved on the device). The current state of the connection is displayed on the configuration website and indicated by the blinking pattern of the status LED on the controller.

## How to build

The easiest way to compile the firmware is to let GitHub do it for you. This repository has GitHub Actions that build the firmware, so you can just fork, enable Actions, make your changes, wait for the job to complete, and look for the binaries in the artifacts produced.

To compile it on your own machine, you will need a Zephyr build environment. You can set it up yourself or you can use Docker. Either way take a look at the [build.sh](build.sh) script. With Docker, a command like this builds the existing variants (start from the top level of the repository):

```
docker run --rm -v $(pwd):/workspace/project -w /workspace/project ghcr.io/zephyrproject-rtos/ci:v0.29.2 ./build.sh
``` 

## License

Code in this repository is licensed under the MIT License. However, the `chiaki-ng` project uses AGPL v3.0, so the final binaries are distributed under the terms of that license.
