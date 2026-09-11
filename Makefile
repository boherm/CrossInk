default:
	@echo "build-upload: 	Build and upload the firmware to the device"
	@echo "simu: 		Build and run the simulator"

build-upload:
	CROSSINK_RELEASE_VERSION="1.6.1-boherm" pio run -e x4-pro -t upload

simu:
	pio run -e x4-pro-simulator -t run_simulator
