.PHONY: all addins firmware test checksums flash install clean

all: addins firmware checksums

addins:
	./tools/build-addins

firmware:
	./tools/build-firmware

test:
	./tools/test

checksums:
	./tools/checksums

flash:
	./tools/flash-esp32 $${PORT:-/dev/ttyACM0}

install:
	./tools/install-calculator

clean:
	$(MAKE) -C apps/CasioWIFI clean FXCGSDK=$${FXCGSDK:-/opt/prizmsdk-linux}
	rm -rf .build
