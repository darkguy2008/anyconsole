import os
import select
import sys
import zlib

from hidtools.device.sony_gamepad import PS4ControllerBluetooth, PS4ControllerUSB
from hidtools.uhid import UHIDDevice

CONTROL = "/tmp/control"
PS_BUTTON = 13
HID_DATA_PRESENT = 0xC0
BLUETOOTH_INPUT_SEED = 0xA1
BLUETOOTH_CRC_OFFSET = 74


class Rumble:
    def output_report(self, data, size, rtype):
        weak, strong = data[self.MOTOR_OFFSET], data[self.MOTOR_OFFSET + 1]
        if weak or strong:
            print(f"rumble {strong} {weak}", flush=True)


class UsbPad(Rumble, PS4ControllerUSB):
    MOTOR_OFFSET = 4


class BluetoothPad(Rumble, PS4ControllerBluetooth):
    MOTOR_OFFSET = 6

    def __init__(self):
        super().__init__()
        self.battery.cable_connected = False

    def create_report(self, **arguments):
        report = super().create_report(**arguments)
        report[1] = HID_DATA_PRESENT
        crc = zlib.crc32(bytes(report[:BLUETOOTH_CRC_OFFSET]), zlib.crc32(bytes([BLUETOOTH_INPUT_SEED])))
        report[BLUETOOTH_CRC_OFFSET:] = crc.to_bytes(len(report) - BLUETOOTH_CRC_OFFSET, "little")
        return report


BUSES = {"usb": UsbPad, "bluetooth": BluetoothPad}


def obey(pad, words):
    if words == ["press"]:
        pad.event(buttons={PS_BUTTON: True})
    elif words == ["release"]:
        pad.event(buttons={PS_BUTTON: False})
    elif words[0] == "battery":
        pad.battery.capacity = int(words[1])
        pad.event()
    else:
        print(f"unknown command: {' '.join(words)}", file=sys.stderr, flush=True)


def main():
    pad = BUSES[sys.argv[1]]()
    pad.buttons += (PS_BUTTON,)
    pad.create_kernel_device()
    os.mkfifo(CONTROL)
    control = os.open(CONTROL, os.O_RDWR)
    pending = b""
    print("created", flush=True)
    while True:
        readable = select.select([pad.fd, control], [], [])[0]
        if pad.fd in readable:
            UHIDDevice.dispatch(0)
        if control not in readable:
            continue
        *lines, pending = (pending + os.read(control, select.PIPE_BUF)).split(b"\n")
        for words in filter(None, (line.decode().split() for line in lines)):
            if words == ["unplug"]:
                pad.destroy()
                return
            obey(pad, words)


main()
