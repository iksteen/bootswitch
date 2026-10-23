.PHONY: test firmware efi
test:
	mkdir -p build
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -Iinclude src/boot_disk.cpp tests/boot_disk_test.cpp -o build/boot_disk_test
	./build/boot_disk_test build
	python3 tests/check_fat.py
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -Iinclude src/boot_disk.cpp src/virtual_disk.cpp src/settings.cpp \
	  tests/config_disk_test.cpp -o build/config_disk_test
	./build/config_disk_test build
	python3 tests/check_config_fat.py
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -DBOOTSWITCH_ENABLE_USB_UPDATE=1 \
	  -Itests/firmware_update_stubs -Iinclude src/boot_disk.cpp src/virtual_disk.cpp \
	  src/firmware_update.cpp tests/firmware_update_test.cpp -o build/firmware_update_test
	./build/firmware_update_test build
	python3 tests/check_firmware_fat.py
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -Iinclude src/ble_command.cpp tests/ble_command_test.cpp -o build/ble_command_test
	./build/ble_command_test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -DBOOTSWITCH_ENABLE_REMOTE_FLASH=1 \
	  -Iinclude src/ble_command.cpp tests/ble_command_test.cpp -o build/ble_command_dev_test
	./build/ble_command_dev_test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -Itests/ble_power_stubs -Iinclude src/ble_power.cpp tests/ble_power_test.cpp \
	  -o build/ble_power_test
	./build/ble_power_test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -Iinclude src/display_telemetry.cpp tests/display_telemetry_test.cpp \
	  -o build/display_telemetry_test
	./build/display_telemetry_test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -Itests/usb_wake_stubs -Iinclude src/usb_wake.cpp tests/usb_wake_test.cpp \
	  -o build/usb_wake_test
	./build/usb_wake_test
	python3 tests/check_usb_dcd.py
	python3 tests/check_linux_wake.py

firmware:
	pio run

efi:
	$(MAKE) -C efi
