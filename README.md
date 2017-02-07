# Sabumnim's VEX Cortex Binary flasher
An STM32 bootloader flasher program optimized for the VEX Cortex and which can download only differences. Based on jpearman's [cortexflash project](https://github.com/jpearman/stm32flashCortex)

## Features
* Fast downloads (assuming little file change per compilation)
* Cached download
* Intel HEX format support
* Force option to flash entire binary instead of diff
* Quiet option to minimize output
* Execute option to (re)start robot and show information, skipping downloading completely
* Works on Windows and \*nix systems (hopefully)

#### About Binary Format Support
The empty memory in binary files is filled with 0's, making it impossible to differentiate blank memory against actual 0's in memory. This wouldn't be a problem except that the STM32Fxxx chips (like the one in the VEX Cortex) erase memory to all high bits (1 bits or 0xff bytes). With the Intel HEX format, empty space is skipped in the file, so it is trivial to skip flashing that empty data instead.

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
