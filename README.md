# argon-one-fan-driver

This is a Aragon one fan driver, which uses the native Linux thermal framework
for controlling the fan speed.

## Compilation

### Driver
`make -j` will produce the driver .ko file.

### DTS
cd into the `dts` dirctory, then  `make` will produce the overlay dtbo file.
