#include "main.h"

// Global variables
serial_t *serial = NULL;
stm32_t *stm = NULL;

void *p_st = NULL;

// Constants
const char zero[4] = {0, 0, 0, 0};

// Settings
serial_baud_t baudRate = SERIAL_BAUD_115200;

char *file = NULL, *port = NULL;

parserPackage_t cacheParser, fileParser;

enum {
  flag_quiet = 0x01,
  flag_force = 0x02,
  flag_help = 0x04,
  flag_execute = 0x08,
};

int flags = 0;
bool fInit = true;

int main(int argc, char* argv[]) {
  int result = 0;


  parseOptions(argc, argv);

  if(flags & flag_execute) {
    if(port == NULL) {
      printf("Not enough arguments (port is undefined)\n");

      showHelp(argv[0]);
      return 1;
    }
  } else if(flags & flag_help) {
    showHelp(argv[0]);

    return 1;
  } else {
    if(file == NULL) {
      printf("Not enough arguments (filename is undefined)\n");

      showHelp(argv[0]);
      return 1;
    } else if(port == NULL) {
      printf("Not enough arguments (port is undefined)\n");

      showHelp(argv[0]);
      return 1;
    }
  }

  if((!(flags & flag_execute) && (file == NULL || port == NULL)) || (flags & flag_execute && port == NULL)) {
    printf("Not enough arguments\n");

    showHelp(argv[0]);
    return 1;
  }

  // Show help?
  if(flags & flag_help) {
    showHelp(argv[0]);
    return 1;
  }

  // Show debug info?
  if(!(flags & flag_quiet)) {
    printf("Sabumnim's VEX cortex binary flasher\n");
    printf("Working directory %s\n\n", getcwd(NULL, 0));
  }

  // Open serial device
  serial = serial_open(port);
  if(!serial) {
    fprintf(stderr, "Error: Could not open serial port %s\n", port);
    cleanup();
    return -1;
  }

  // Make sure we are in boot load mode
  if(testBootloader()) {
    result = init();
    if(result != 1) {
      cleanup();
      return -1;
    }
  }

  // Setup serial port for bootloader
  result = serial_setup(serial, baudRate, SERIAL_BITS_8, SERIAL_PARITY_EVEN, SERIAL_STOPBIT_1);
  if(result != SERIAL_ERR_OK) {
    fprintf(stderr, "Error: Could not configure serial port %s\n", port);
    cleanup();
    return -1;
  }

  // 100ms delay before comms start
  uSleep(100000);

  // RTS needs to be low for user program to be reset - no idea why
  // May need to do something with the DIR line for the USB, not sure yet
  serial_set_rts(serial, 0);

  // 100ms delay before comms start
  uSleep(100000);

  // Initialize the STM32 communications (we may already be in bootload mode)
  stm = stm32_init(serial, fInit);
  if(!stm) {
    fprintf(stderr, "Error: No STM32 device connected on serial port %s\n", port);
    cleanup();
    return -1;
  }

  if(!(flags & flag_quiet)) {
    // Print some info about the cortex
    printf("Version      : 0x%02x\n", stm->bl_version);
    printf("Option 1     : 0x%02x\n", stm->option1);
    printf("Option 2     : 0x%02x\n", stm->option2);
    printf("Device ID    : 0x%04x (%s)\n", stm->pid, stm->dev->name);
    printf("RAM          : %dKiB  (%db reserved by bootloader)\n", (stm->dev->ram_end - 0x20000000) / 1024, stm->dev->ram_start - 0x20000000);
    printf("Flash        : %dKiB (sector size: %dx%d)\n", (stm->dev->fl_end - stm->dev->fl_start ) / 1024, stm->dev->fl_pps, stm->dev->fl_ps);
    printf("Option RAM   : %db\n", stm->dev->opt_end - stm->dev->opt_start);
    printf("System RAM   : %dKiB\n", (stm->dev->mem_end - stm->dev->mem_start) / 1024);
  }

  if(!(flags & flag_execute)) {
    uint8_t cacheBuffer[256], fileBuffer[256], c;
    uint32_t addr = stm->dev->fl_start;
    size_t len, cacheSize, fileSize, offset = 0,
      maxSize;
    int i = 0, diffLen;
    diff_t *difference;


    // TODO: try multiple parsers

    // Load cached file if used
    if(flags & flag_force) {
      cacheParser = initParser(kStorageType_hex);

      result = cacheParser.parser->open(cacheParser.storage, "cortex.cache");
      if(result != kParserError_none) {
        cacheParser.parser->close(cacheParser.storage);
        printf("Cached file is either nonexistant or corrupt - defaulting to complete re-flash\n");
        flags |= flag_force;
      }

      if(flags & flag_force) {
        cacheSize = cacheParser.parser->size(cacheParser.storage);

        if(cacheSize > stm->dev->fl_end - stm->dev->fl_start) {
          printf("Cached file is larger than available flash space - defaulting to complete re-flash\n");
        }
      }
    }


    fileParser = initParser(kStorageType_hex);

    result = fileParser.parser->open(fileParser.storage, file);
    if(result != kParserError_none) {
      cleanup();
      fprintf(stderr, "Provided file is either nonexistant or corrupt\n");
      return -1;
    }

    fileSize = fileParser.parser->size(fileParser.storage);

    if(fileSize > stm->dev->fl_end - stm->dev->fl_start) {
      cleanup();
      fprintf(stderr, "Provided file is larger than available flash space\n");
      return -1;
    }

    if(flags & flag_force) {
      // Old flashing method
      stm32_erase_memory(stm, 0xff);

      // TODO: show progress
      // TODO: time download

      while(addr < stm->dev->fl_end && offset < fileSize) {
        len = stm->dev->fl_end - addr;
        len = sizeof(fileBuffer) > len ? len : sizeof(fileBuffer);
        len = len > fileSize - offset ? fileSize - offset : len;

        result = fileParser.parser->read(fileParser.storage, fileBuffer, offset, &len);
        if(result != kParserError_none) {
          cleanup();
          return -1;
        }

        // TODO: verify download?
        result = stm32_write_memory(stm, addr, fileBuffer, len);

        if(!result) {
          fprintf(stderr, "\nFailed to write memory at address 0x%08x\n", addr);
          cleanup();
          return -1;
        }

        addr += len;
        offset += len;

        // TODO: show progress
      }
    } else {
      if(cacheSize > fileSize)
        maxSize = cacheSize;
      else
        maxSize = cacheSize;


      difference = calloc(sizeof(diff_t) * maxSize / 256, 1);

      // TODO: show progress
      // TODO: time difference calculation

      // Calculate differencex
      while(addr < stm->dev->fl_end && addr < maxSize) {
        // Deal with file size differences
        if(addr > fileSize) {
          len = 256 > maxSize - addr ? maxSize - addr : 256;

          // Because cache is larger, don't read input file
          difference[i].clear = true;
          difference[i].len = len;
          difference[i].offset = offset;

          offset += len;
          addr += 256;
          i++;

          continue;
        } else if(addr > cacheSize) {
          len = 256 > maxSize - addr ? maxSize - addr : 256;

          difference[i].len = len;
          difference[i].offset = offset;

          offset += len;
          addr += 256;
          i++;

          continue;
        }

        len = 4;

        // Compute up to a 256 byte difference
        for(c = 0; c < 256;) {
          result = fileParser.parser->read(fileParser.storage, fileBuffer, offset + c, &len);
          if(result != kParserError_none) {
            cleanup();
            return -1;
          }

          // Make sure we don't compare buffers which are inherently different, if we are at end of file
          if(len < 4) {
            switch(len) {
              case 1:
                fileBuffer[1] = 0;
              case 2:
                fileBuffer[2] = 0;
              case 3:
                fileBuffer[3] = 0;
            }
          }

          result = cacheParser.parser->read(cacheParser.storage, cacheBuffer, offset + c, &len);
          if(result != kParserError_none) {
            cleanup();
            return -1;
          }

          // Make sure we don't compare buffers which are inherently different, if we are at end of file
          if(len < 4) {
            switch(len) {
              case 1:
                cacheBuffer[1] = 0;
              case 2:
                cacheBuffer[2] = 0;
              case 3:
                cacheBuffer[3] = 0;
            }
          }

          if((uint32_t)cacheBuffer == (uint32_t)fileBuffer) {
            if(c > 0)
              break;

            offset += 4;
            addr += 4;
          } else
            c += 4;
        }

        difference[i].len = c;
        difference[i].offset = offset;

        offset += c;
        addr += c;
        i++;
      }

      diffLen = i + 1;
      addr = stm->dev->fl_start;

      // TODO: show progress
      // TODO: time download

      // Flash differences
      for(i = 0; i < diffLen; i++) {
        len = difference[i].len;

        if(difference[i].clear)
          memset(fileBuffer, 0, len);
        else
          fileParser.parser->read(fileParser.storage, fileBuffer, difference[i].offset, &len);

        result = stm32_write_memory(stm, addr + offset, fileBuffer, len);
      }

      // TODO: show progress

      free(difference);
    }
  }

  // Execute code
  if(stm) {
    if(!(flags & flag_quiet)) {
      fprintf(stdout, "\nStarting execution at address 0x%08x... \n", stm->dev->fl_start);
      fflush(stdout);
    }

    result = stm32_go(stm, stm->dev->fl_start);
    if(!(flags & flag_quiet)) {
      if(result)
        fprintf(stdout, "done.\n");
      else
        fprintf(stdout, "failed.\n");
    }

    cleanup();

    printf("\n");
    return 1;
  }

  return 0;
}

bool testBootloader() {
  char buf[2] = {0x7f};
  char rep[16] = {0};
  int retry;

  uSleep(100000);

  if(serial) {
    // Setup serial port for bootloader
    if(serial_setup(serial, baudRate, SERIAL_BITS_8, SERIAL_PARITY_EVEN, SERIAL_STOPBIT_1) != SERIAL_ERR_OK) {
      perror(port);
      cleanup();
      return -1;
    }

    uSleep(100000);

    for(retry = 0; retry < 5; retry++) {
      serial_write(serial, buf, 1);
      if(serial_read(serial, rep, 1) == SERIAL_ERR_OK) {
        // Let's see what we got
        if(rep[0] == 0x79) {
          // We are done, user must have pushed program button
          fInit = false; // remove init flag
          return 0;
        } else if(rep[0] == 0x1f) {
          // We are also done, see if we can get a status
          buf[0] = 0x00;
          buf[1] = 0xff;
          serial_write(serial, buf, 2);
          // check status is good
          if(serial_read(serial, rep, 15) == SERIAL_ERR_OK && rep[0] == 0x79) {
            fInit = false; // remove init flag
            return 0;
          }
        }
      }
    }
  }

  return fInit;
}

bool parseOptions(int argc, char *argv[]) {
  char *arg;
  int i, iArg, iOpt = 0;
  char optionType;
  /*
    0: other argument
    1: single letter option
    2: word option
  */

  for(iArg = 1; iArg < argc; iArg++) {
    arg = argv[iArg];
    optionType = 0;

    for(i = 0; ; i++) {
      if(i == 0 && arg[i] == '-')
        optionType = 1;
      else if(i == 1 && arg[i] == '-')
        optionType = 2; // not used yet
      else {
        if(arg[i] == 0) {
          // End of string

          // Store ordinal and anonymous options
          if(optionType == 0) {
            switch(iOpt) {
              case 0:
                if(flags & flag_execute)
                  port = arg;
                else
                  file = arg;
                break;

              case 1:
                if(flags & flag_execute)
                  continue;

                port = arg;
                break;
            }

            iOpt++;
          }

          break;
        } else if(optionType == 1) {
          // Single character flag

          // Store flags
          switch(arg[i]) {
            case 'x':
              flags |= flag_execute;
              break;

            case 'f':
              flags |= flag_force;
              break;

            case 'q':
              flags |= flag_quiet;
              break;

            case 'h':
              flags |= flag_help;
              break;
          }
        }
      }
    }
  }

  return false;
}

int init() {
  uSleep(100000);

  if(serial) {
    if(serial_setup(serial, baudRate, SERIAL_BITS_8, SERIAL_PARITY_NONE, SERIAL_STOPBIT_1) != SERIAL_ERR_OK) {
      perror(port);
      cleanup();
      return -1;
    }

    uSleep(100000);

    // send some zeros, there are bugs in the serial driver
    serial_write(serial, zero, 4);

    uSleep(100000);

    // check system status
    if(!getSystemStatus()) {
      uSleep(100000);

      // Didn't work, so try again
      if(!getSystemStatus()) {
        fprintf(stderr, "Error: No VEX system detected\n");
        return -1;
      }
    }

    enterUserProgram();

    return 1;
  }
  return 0;
}

bool getSystemStatus() {
  unsigned char buf[5] = {0xc9, 0x36, 0xb8, 0x47, 0x21};
  unsigned char rep[16];

  if(serial) {
    if(!(flags & flag_quiet))
      printf("Send system status request\n");

    // Stop the cortex from sending any data
    serial_flush(serial);

    // Try and get status
    if(serial_write(serial, buf, 5) == SERIAL_ERR_OK) {
      if(serial_read(serial, rep, 14) == SERIAL_ERR_OK) {
        if(!(flags & flag_quiet)) {
          // Double check reply
          if(!(rep[0] == 0xaa && rep[1] == 0x55 && rep[2] == 0x21 && rep[3] == 0x0a))
            return 0;

          // Show reply
          printf("Status ");
          for(int i=0; i<14; i++)
            printf("%02X ", rep[i]);
          printf("\n");

          // Decode some info
          printf("Connection Type  : ");
          switch(rep[11] & 0x30) {
            case 0x10:
              printf("USB Tether\n");
              break;

            case 0x20:
              printf("USB Direct Connection\n");
              break;

            case 0x00:
              printf("WiFi (VEXnet 1.0)\n");
              break;

            case 0x04:
              printf("WiFi (VEXnet 2.0)\n");
              break;

            default:
              printf("Unknown (%02X)\n", rep[11]);
              break;
          }

          if((rep[11] & 0x30) != 0x20)
            printf("Joystick Firmware: %d.%02d\n", rep[4], rep[5]);
          else
            printf("Joystick Firmware: NA\n");

          printf("Master firmware  : %d.%02d\n", rep[6], rep[7]);
          printf("Joystick battery : %.2fV\n", (double)rep[8]  * 0.059);
          printf("Cortex battery   : %.2fV\n", (double)rep[9]  * 0.059);
          printf("Backup battery   : %.2fV\n", (double)rep[10] * 0.059);

          printf("\n");
        }
      } else
        return 0;
    } else
      return 0;
  }

  return 1;
}

void enterUserProgram() {
  char buf[5] = {0xc9, 0x36, 0xb8, 0x47, 0x25};

  if(serial) {
    if(!(flags & flag_quiet))
      printf("Send bootloader start command\n");

    for(char i=0; i<5; i++)
      serial_write(serial, buf, 5);

    uSleep(250000);
  }
}

void showHelp(char *programName) {
  fprintf(stderr,
    "Usage:\n"
#ifdef __WIN32__
    "  %s [-qf] filename COM1\n"
#else
    "  %s [-qf] filename /dev/tty.usbserial\n"
#endif
    "--or--\n"
    "  %s -h\n"
    "--or--\n"
#ifdef __WIN32__
    "  %s -x COM1\n"
#else
    "  %s -x /dev/tty/usbserial\n"
#endif
    "\n"
    "    -q        Quiet mode\n"
    "    -f        Force full flash\n"
    "    -x        Enter VEX user program mode (using C9 commands)\n"
    "    -h        Show this help\n"
    "\n"
    "Examples:\n"
    "    Get device information:\n"
#ifdef __WIN32__
    "      %s -x COM1\n"
#else
    "      %s -x /dev/tty.usbserial\n"
#endif
    "\n"
    "    Write with verify and then start execution:\n"
#ifdef __WIN32__
    "      %s filename COM1\n"
#else
    "      %s filename /dev/tty.usbserial\n"
#endif
    "",
    programName,
    programName,
    programName,
    programName,
    programName
  );
}

void cleanup() {
  uSleep(20000);

  if(cacheParser.storage)
    cacheParser.parser->close(cacheParser.storage);

  if(fileParser.storage)
    fileParser.parser->close(fileParser.storage);

  if(stm)
    stm32_close(stm);

  if(serial)
    serial_close(serial);
}
