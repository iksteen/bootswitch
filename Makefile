.PHONY: test firmware efi
test:
	mkdir -p build
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
	  -Iinclude src/boot_disk.cpp tests/boot_disk_test.cpp -o build/boot_disk_test
	./build/boot_disk_test build
	python3 tests/check_fat.py

firmware:
	pio run

efi:
	$(MAKE) -C efi
