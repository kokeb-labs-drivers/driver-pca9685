# PCA9685 host example

The driver on your PC, with no hardware: the host port in
[`host_pca9685_port.hpp`](host_pca9685_port.hpp) connects the real driver to a simulated PCA9685 (the register model
the host tests use), and [`main.cpp`](main.cpp) starts it at 50 Hz, fades LED0 and centres a servo on LED1.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/pca9685_host_example
```

## Expected output

```text
PCA9685 driver 0.1.0 on the host
pca9685: initialised
PWM 50 Hz (PRE_SCALE 121)
LED0 duty 0/4096
LED0 duty 1024/4096
LED0 duty 2048/4096
LED0 duty 3072/4096
LED0 duty 4096/4096
servo on LED1: 1498160 ns pulse
```

The host tests build this folder too and run the example (CTest `pca9685_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: start, set channels, check for a reset, shut down |
| [`host_pca9685_port.hpp`](host_pca9685_port.hpp) | The host port: bus hooks over the simulated chip, `DelayUs()` and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
