"""Exercise dynamic wake discovery against temporary sysfs trees, without root."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "linux_wake", Path(__file__).resolve().parents[1] / "host/bootswitch_usb_wake.py")
wake = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wake)


class LinuxWakeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "sys"
        self.bus = self.root / "bus/usb/devices"
        self.bus.mkdir(parents=True)
        (self.root / "devices").mkdir()

    def node(self, relative, state=None):
        node = self.root / "devices" / relative
        node.mkdir(parents=True, exist_ok=True)
        if state is not None:
            (node / "power").mkdir(exist_ok=True)
            (node / "power/wakeup").write_text(state + "\n")
        return node

    def application(self, node, product="BootSwitch StickS3"):
        for field, value in {"idVendor": "303a", "idProduct": "1001", "product": product}.items():
            (node / field).write_text(value + "\n")
        alias = self.bus / node.name
        alias.symlink_to(node, target_is_directory=True)
        return alias

    def test_follows_current_topology_and_leaves_siblings_alone(self):
        # Different controller, USB bus, and hub depth from the development PC.
        controller = self.node("pci0000:40/0000:40:02.0", "disabled")
        root_hub = self.node("pci0000:40/0000:40:02.0/usb9", "disabled")
        hub = self.node("pci0000:40/0000:40:02.0/usb9/9-6", "disabled")
        nested = self.node("pci0000:40/0000:40:02.0/usb9/9-6/9-6.2", "disabled")
        app = self.node("pci0000:40/0000:40:02.0/usb9/9-6/9-6.2/9-6.2.1", "disabled")
        sibling = self.node("pci0000:40/0000:40:02.0/usb9/9-5", "disabled")
        alias = self.application(app)
        self.assertEqual(wake.discover(self.root), [alias])
        states = wake.configure_device(alias, self.root)
        self.assertEqual([path.parent.parent for path, _ in states],
                         [app, nested, hub, root_hub, controller])
        self.assertTrue(all(value == "enabled" for _, value in states))
        self.assertEqual((sibling / "power/wakeup").read_text().strip(), "disabled")

    def test_check_and_already_enabled_nodes_do_not_write(self):
        app = self.node("pci/usb12/12-3", "disabled")
        alias = self.application(app)
        with patch.object(Path, "write_text", side_effect=AssertionError("Unexpected write")):
            self.assertEqual(wake.configure_device(alias, self.root, check=True),
                             [(app / "power/wakeup", "disabled")])
        (app / "power/wakeup").write_text("enabled\n")
        with patch.object(Path, "write_text", side_effect=AssertionError("Unexpected write")):
            self.assertEqual(wake.configure_device(alias, self.root),
                             [(app / "power/wakeup", "enabled")])

    def test_tqt_uses_the_same_dynamic_ancestor_discovery(self):
        hub = self.node("pci/usb3", "disabled")
        app = self.node("pci/usb3/3-8", "disabled")
        alias = self.application(app, "BootSwitch T-QT")
        self.assertEqual(wake.discover(self.root), [alias])
        states = wake.configure_device(alias, self.root)
        self.assertEqual([path.parent.parent for path, _ in states], [app, hub])
        self.assertTrue(all(value == "enabled" for _, value in states))

    def test_excludes_bootloader_and_other_devices(self):
        bootloader = self.node("pci/usb6/6-2", "disabled")
        alias = self.application(bootloader, "USB JTAG/serial debug unit")
        self.assertEqual(wake.discover(self.root), [])
        with self.assertRaisesRegex(ValueError, "not the BootSwitch"):
            wake.configure_device(alias, self.root)
        self.assertEqual((bootloader / "power/wakeup").read_text().strip(), "disabled")

    def test_skips_missing_and_unsupported_ancestor_attributes(self):
        unsupported = self.node("pci", "")
        app = self.node("pci/usb2/2-8", "disabled")
        alias = self.application(app)
        self.assertEqual(wake.configure_device(alias, self.root),
                         [(app / "power/wakeup", "enabled")])
        self.assertEqual((unsupported / "power/wakeup").read_text().strip(), "")

    def test_rejects_device_without_wake_capability(self):
        app = self.node("pci/usb1/1-4", "")
        alias = self.application(app)
        with self.assertRaisesRegex(ValueError, "does not advertise USB wake"):
            wake.configure_device(alias, self.root)

    def test_rejects_symlink_outside_devices_tree(self):
        outside = self.root / "elsewhere"
        outside.mkdir()
        alias = self.application(outside)
        with self.assertRaisesRegex(ValueError, "inside the sysfs"):
            wake.configure_device(alias, self.root)

    def test_write_failure_is_reported(self):
        app = self.node("pci/usb4/4-9", "disabled")
        alias = self.application(app)
        with patch.object(Path, "write_text", side_effect=PermissionError("permission denied")):
            with self.assertRaises(PermissionError):
                wake.configure_device(alias, self.root)


if __name__ == "__main__":
    unittest.main()
