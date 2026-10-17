import os
import select
import sys
import zlib

from hidtools import hid
from hidtools.device.base_gamepad import BaseGamepad
from hidtools.device.sony_gamepad import PS4ControllerBluetooth, PS4ControllerUSB
from hidtools.uhid import UHIDDevice

CONTROL = "/tmp/control"
PS_BUTTON = 13
HID_DATA_PRESENT = 0xC0
BLUETOOTH_INPUT_SEED = 0xA1
BLUETOOTH_CRC_OFFSET = 74
GUIDELESS_BUTTONS = 12
BITS_PER_REPORT_BYTE = 8
PADDING_BITS = -GUIDELESS_BUTTONS % BITS_PER_REPORT_BYTE
AXIS_MAXIMUM = (1 << BITS_PER_REPORT_BYTE) - 1
AXES = ("X", "Y", "Z", "Rz")
AXIS_USAGES = "".join(f" Usage ({axis})\n" for axis in AXES)
GUIDELESS_DESCRIPTOR = hid.ReportDescriptor.from_human_descr(f"""
Usage Page (Generic Desktop)
Usage (Game Pad)
Collection (Application)
 Usage Page (Button)
 Usage Minimum (1)
 Usage Maximum ({GUIDELESS_BUTTONS})
 Logical Minimum (0)
 Logical Maximum (1)
 Report Size (1)
 Report Count ({GUIDELESS_BUTTONS})
 Input (Data,Var,Abs)
 Report Size ({PADDING_BITS})
 Report Count (1)
 Input (Cnst,Arr,Abs)
 Usage Page (Generic Desktop)
{AXIS_USAGES} Logical Minimum (0)
 Logical Maximum ({AXIS_MAXIMUM})
 Report Size ({BITS_PER_REPORT_BYTE})
 Report Count ({len(AXES)})
 Input (Data,Var,Abs)
End Collection
""")


class DualShock:
    def __init__(self):
        super().__init__()
        self.buttons += (PS_BUTTON,)

    def output_report(self, data, size, rtype):
        weak, strong = data[self.MOTOR_OFFSET], data[self.MOTOR_OFFSET + 1]
        if weak or strong:
            print(f"rumble {strong} {weak}", flush=True)


class UsbPad(DualShock, PS4ControllerUSB):
    MOTOR_OFFSET = 4


class BluetoothPad(DualShock, PS4ControllerBluetooth):
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


class GuidelessPad(BaseGamepad):
    def __init__(self):
        super().__init__(GUIDELESS_DESCRIPTOR, name="Guideless Gamepad")
        self.buttons = tuple(range(1, GUIDELESS_BUTTONS + 1))


PADS = {"usb": UsbPad, "bluetooth": BluetoothPad, "guideless": GuidelessPad}


def obey(pad, words):
    if words[0] in ("press", "release"):
        pad.event(buttons={int(words[1]) if len(words) > 1 else PS_BUTTON: words[0] == "press"})
    elif words[0] == "battery":
        pad.battery.capacity = int(words[1])
        pad.event()
    else:
        print(f"unknown command: {' '.join(words)}", file=sys.stderr, flush=True)


def main():
    pad = PADS[sys.argv[1]]()
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
