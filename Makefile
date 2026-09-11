default:
	@echo "build-upload: Build and upload the firmware to the device"

build-upload:
	CROSSINK_RELEASE_VERSION="1.5.1-boherm" pio run -e x4-pro -t upload
