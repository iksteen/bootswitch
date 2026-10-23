"""Compile the shipped DCD interrupt handler with fake peripheral registers.

Tests event translation and FIFO lifetime without an ESP32 or sleeping the PC.
The interrupt handler is extracted verbatim; the earlier application-level
TinyUSB mocks cannot detect a driver translating suspend into unplugged.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess


root = Path(__file__).resolve().parents[1]
source = (root / "src/vendor/dcd_esp32sx.c").read_text()
handler = "static void _dcd_int_handler(void* arg)\n" + source.split(
    "static void _dcd_int_handler(void* arg)\n", 1
)[1].split("\nvoid dcd_int_enable", 1)[0]

# Only the register/event boundary is fake. Use separate symbolic bits; the
# hardware values are supplied by the ESP32 SDK in the real firmware build.
names = sorted(set(re.findall(r"\bUSB_[A-Z0-9_]+", handler)))
assert len(names) <= 32
prefix = "\n".join(f"#define {name} (UINT32_C(1) << {i})"
                   for i, name in enumerate(names))
harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ESP_EARLY_LOGV(...) ((void)0)
enum { DCD_EVENT_BUS_RESET, DCD_EVENT_UNPLUGGED, DCD_EVENT_SOF,
       DCD_EVENT_SUSPEND, DCD_EVENT_RESUME, TUSB_SPEED_FULL };
static struct { uint32_t gintmsk, gintsts, gotgint; } USB0;
static unsigned _allocated_fifos, reset_count, enum_count;
static unsigned event_count, last_event;
static void bus_reset(void) { ++reset_count; }
static void enum_done_processing(void) { ++enum_count; }
static void dcd_event_bus_signal(uint8_t port, unsigned event, bool isr) {
    assert(port == 0 && isr);
    ++event_count;
    last_event = event;
}
static void dcd_event_bus_reset(uint8_t port, unsigned speed, bool isr) {
    assert(speed == TUSB_SPEED_FULL);
    dcd_event_bus_signal(port, DCD_EVENT_BUS_RESET, isr);
}
static void read_rx_fifo(void) {}
static void handle_epout_ints(void) {}
static void handle_epin_ints(void) {}
""" + prefix + "\n" + handler + r"""
static void interrupt(uint32_t flags, uint32_t otg) {
    USB0.gintmsk = UINT32_MAX;
    USB0.gintsts = flags;
    USB0.gotgint = otg;
    event_count = 0;
    _dcd_int_handler(NULL);
}
int main(void) {
    _allocated_fifos = 4;
    interrupt(USB_USBSUSP_M, 0);
    assert(event_count == 1 && last_event == DCD_EVENT_SUSPEND);
    assert(_allocated_fifos == 4 && reset_count == 0);
    interrupt(USB_WKUPINT_M, 0);
    assert(event_count == 1 && last_event == DCD_EVENT_RESUME);
    assert(_allocated_fifos == 4 && reset_count == 0);
    interrupt(USB_OTGINT_M, 0); // Other OTG interrupts do not detach.
    assert(event_count == 0 && _allocated_fifos == 4);
    interrupt(USB_OTGINT_M, USB_SESENDDET_M);
    assert(event_count == 1 && last_event == DCD_EVENT_UNPLUGGED);
    assert(_allocated_fifos == 1);
    _allocated_fifos = 4;
    interrupt(USB_USBRST_M | USB_RESETDET_M, 0);
    assert(reset_count == 1 && _allocated_fifos == 1);
    _allocated_fifos = 4;
    interrupt(USB_RESETDET_M, 0);
    assert(reset_count == 2 && _allocated_fifos == 1);
    interrupt(USB_ENUMDONE_M, 0);
    assert(enum_count == 1 && event_count == 1 && last_event == DCD_EVENT_BUS_RESET);
    puts("Shipped USB ISR: suspend/resume preserve FIFOs; unplug/reset still clear them.");
}
"""
build = root / "build"
build.mkdir(exist_ok=True)
test_source = build / "usb_dcd_test.c"
test_binary = build / "usb_dcd_test"
test_source.write_text(harness)
subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
    "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
    str(test_source), "-o", str(test_binary)
], check=True)
subprocess.run([str(test_binary)], check=True)
