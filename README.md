# ssm

**NOTE: Development in this repo has moved to [ssm-lang/ssm-runtime](github.com/ssm-lang/ssm-runtime).**

----

The Sparse Synchronous Model: A deterministic real-time execution
technique that allows explicit, precise timing control.

An early version of this system is described in our FDL 2021 paper
<http://www.cs.columbia.edu/~sedwards/papers/edwards2020sparse.pdf>.
The artifact accompanying that paper may be found [here](https://github.com/sedwards-lab/ssm/releases/tag/fdl2020).

Documentation: http://sedwards-lab.github.io/ssm-doc

To build and test the runtime system on your host,

1. `make`

To run the examples on embedded hardware,

1. Install the PlatformIO Core (CLI) build system from https://platformio.org/

2. Under Linux, you may need to install the `99-platformio-udev.rules` file
   to enable permissions to access your debugging probe.
   See https://docs.platformio.org/en/latest/faq.html#platformio-udev-rules

3. cd into, e.g., examples/blink-platformio-zephyr

4. E.g., for the Nordic nrf82840 discovery kit,
   `pio run --environment nrf52840_dk --target upload --target monitor`
   will compile, upload, and display the serial output of the example.
   The monitor target is optional.
   See the `platformio.ini` file for details about other targets.

   Note that PlatformIO will download and compile a separate copy of this
   (ssm runtime) library from github, not the one a few directories
   above the examples.
`
