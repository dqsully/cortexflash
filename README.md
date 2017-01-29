# Sabumnim's VEX Cortex Binary flasher
An STM32 bootloader flasher program optimized for the VEX Cortex and downloads only differences.

## Features
* Fast downloads (assuming little file change per compilation)
* Cached download
* Intel HEX ~~and Binary~~ format support
* Force option to flash entire binary instead of diff
* Quiet option to minimize output
* Execute option to (re)start robot and show information, skipping downloading completely
* Works on Windows and \*nix systems

#### Windows
```
C:\>cortexflash -h
Usage:
  cortexflash [-qf] filename COM1
--or--
  cortexflash -h
--or--
  cortexflash -x COM1

    -q        Quiet mode
    -f        Force full flash
    -x        Enter VEX user program mode (using C9 commands)
    -h        Show this help

Examples:
    Get device information:
      cortexflash -x COM1

    Write with verify and then start execution:
      cortexflash filename COM1

```
#### macOS
```
user@Computer:/home$ cortexflash -h
Usage:
  ./cortexflash [-qf] filename /dev/tty.usbserial
--or--
  ./cortexflash -h
--or--
  ./cortexflash -x /dev/tty/usbserial

    -q        Quiet mode
    -f        Force full flash
    -x        Enter VEX user program mode (using C9 commands)
    -h        Show this help

Examples:
    Get device information:
      ./cortexflash -x /dev/tty.usbserial

    Write with verify and then start execution:
      ./cortexflash filename /dev/tty.usbserial
```
