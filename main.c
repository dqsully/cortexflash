/*
  stm32flash - Open Source ST STM32 flash program for *nix
  Copyright (C) 2010 Geoffrey McRae <geoff@spacevs.com>
..Copyright (C) 2011 Steve Markgraf <steve@steve-m.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
/*
  Also based on jpearman's cortexflash project at
  https://github.com/jpearman/stm32flashCortex
*/

#include "main.h"

// Global variables
serial_t *serial = NULL;
stm32_t *stm = NULL;
parserPackage_t cacheParser, fileParser;

// Constants
const char zero[4] = {0, 0, 0, 0};

// Settings
serial_baud_t baudRate = SERIAL_BAUD_115200;
char *file = NULL, *port = NULL;


enum {
  flag_quiet = 0x01,
  flag_force = 0x02,
  flag_help = 0x04,
  flag_execute = 0x08,
};

int flags = 0;
bool fInit = true;

void beginTimer () {

}

int cp(const char *to, const char *from)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    fd_to = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_to < 0)
        goto out_error;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);

        /* Success! */
        return 0;
    }

  out_error:
    saved_errno = errno;

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    errno = saved_errno;
    return -1;
}

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
    uint8_t cacheBuffer[2048], fileBuffer[2048], cs;
    uint32_t addr = stm->dev->fl_start;
    size_t len, cacheSize, fileSize, offset = 0, skip, bytesFlashed, flen,
      maxSize, minSize;
    int i = 0, diffLen;
    short c;
    diff_t *difference;
    bool different;


    // TODO: try multiple parsers

    // Load cached file if used
    if(!(flags & flag_force)) {
      cacheParser = initParser(kStorageType_hex);

      result = cacheParser.parser->open(cacheParser.storage, "cortex.cache");
      if(result != kParserError_none) {
        cacheParser.parser->close(cacheParser.storage);
        printf("Cached file is either nonexistant or corrupt - defaulting to complete re-flash\n");
        flags |= flag_force;
      }

      if(!(flags & flag_force)) {
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
      fprintf(stderr, "Provided file is either nonexistant or corrupt (%i)\n", result);
      return -1;
    }

    fileSize = fileParser.parser->size(fileParser.storage);

    if(fileSize > stm->dev->fl_end - stm->dev->fl_start) {
      cleanup();
      fprintf(stderr, "Provided file is larger than available flash space\n");
      return -1;
    }

    if(flags & flag_force) {
      printf("\nFlashing Everything\n");
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
      printf("\nFlashing Differences\n");
      if(cacheSize > fileSize) {
        minSize = fileSize;
        maxSize = cacheSize;
      } else {
        minSize = cacheSize;
        maxSize = fileSize;
      }

      // The page size is 2k, so that is our minimum size to erase/write
      difference = calloc(sizeof(diff_t) * maxSize / stm->dev->fl_ps + 1, 1);
      printf("Elements Allocated: %i\n", maxSize / stm->dev->fl_ps + 1);

      // TODO: show progress
      // TODO: time difference calculation

      // TODO: overhaul diff calculation because memory needs to be erased first
      // Calculate differences
      while(addr + offset < stm->dev->fl_end && offset < maxSize) {
        // Deal with file size differences
        if(offset >= fileSize) {
          len = stm->dev->fl_ps > maxSize - offset ? maxSize - offset : stm->dev->fl_ps;

          // Because cache is larger, don't read input file
          difference[i].clear = true;
          difference[i].len = len;
          difference[i].offset = offset;

          offset += len;
          addr += len;
          i++;

          continue;
        } else if(offset > cacheSize) {
          len = stm->dev->fl_ps > maxSize - offset ? maxSize - offset : stm->dev->fl_ps;

          difference[i].len = len;
          difference[i].offset = offset;

          offset += len;
          addr += len;
          i++;

          continue;
        }

        different = false;
        for(c = 0; c < stm->dev->fl_ps; c += 4) {
          if(offset + c >= minSize)
            break;

          len = 4;

          result = fileParser.parser->read(fileParser.storage, fileBuffer + c, offset + c, &len);
          if(result != kParserError_none) {
            printf("Error reading file (%i, %i, %i, %i, %i)\n", result, addr, offset + c, minSize, maxSize);
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

          len = 4;

          result = cacheParser.parser->read(cacheParser.storage, cacheBuffer + c, offset + c, &len);
          if(result != kParserError_none) {
            printf("Error reading cache (%i, %i, %i, %i, %i)\n", result, addr, offset + c, minSize, maxSize);
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

          if(*(uint32_t*)(cacheBuffer + c) != *(uint32_t*)(fileBuffer + c))
            different = true;
        }

        if(different) {
          difference[i].offset = offset;

          if(c != stm->dev->fl_ps)
            difference[i].len = stm->dev->fl_ps > maxSize - offset ? maxSize - offset : stm->dev->fl_ps;
          else
            difference[i].len = c;

          i++;
        }

        offset += c;
        addr += c;
      }

      diffLen = i;
      printf("Elements Used: %i\n", diffLen);
      addr = stm->dev->fl_start;

      // Erase relevant pages in memory
      result = stm32_send_command(stm, stm->cmd->er);
      if(!result) {
        printf("Failed to erase memory pages\n");
        cleanup();
        return -1;
      }

      // Make i represent the number of pages (cuz I'm lazy)
      i -= 1;
      cs = i;

      // Send number of pages
      stm32_send_byte(stm, i);

      // Send page numbers to erase
      for(c = 0; c <= i; c++) {
        stm32_send_byte(stm, difference[c].offset / stm->dev->fl_ps);
        cs ^= difference[c].offset / stm->dev->fl_ps;

        printf("Erasing page %li\n", difference[c].offset / stm->dev->fl_ps);
      }

      // Send checksum
      stm32_send_byte(stm, cs);

      result = stm32_read_byte(stm);
      if(result != STM32_ACK) {
        printf("Failed to erase memory pages\n");
        cleanup();
        return -1;
      }

      // Flash Differences
      for(i = 0; i < diffLen; i++) {
        len = difference[i].len;
        offset = difference[i].offset;

        // Skip rewriting nothing
        if(difference[i].clear)
          continue;

        printf("Writing %i bytes at %li (%li)...", len, difference[i].offset, addr + difference[i].offset);

        skip = 0;

        for(c = 0; len > 0; c++) {

          flen = bytesFlashed = len >= 256 ? 256 : len;

          memset(fileBuffer, 0xff, 256);
          fileParser.parser->read(fileParser.storage, fileBuffer, offset, &bytesFlashed);

          // Trim beginning of 256 bytes
          while(*(uint32_t*)(fileBuffer + skip) == 0xffffffff && bytesFlashed > 0) {
            skip += 4;
            bytesFlashed -= 4;
          }

          // Trim end of 256 bytes
          while(*(uint32_t*)(fileBuffer + skip + bytesFlashed - 4) == 0xffffffff && bytesFlashed > 0)
            bytesFlashed -= 4;

          // Make sure we don't try flashing if there are not bytes to flash
          if(bytesFlashed > 0) {
            result = stm32_write_memory(stm, addr + offset, fileBuffer + skip, bytesFlashed);
            if(!result)
              printf("Failed to write memory at address 0x%08x\n", addr + offset);
          }

          printf("%i,", bytesFlashed);

          len -= flen;
          offset += flen;
        }

        printf("done\n");
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
  }

  cleanup();

  printf("\n");

  // Cache file
  if(!(flags & flag_execute || flags & flag_help)) {
    printf("Caching input file\n");
    result = cp("cortex.cache", file);
    if(result != 0)
      printf("Copying failed\n");
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
          switch(rep[11] & 0x34) {
            case 0x10:
            case 0x14:
              printf("USB Tether\n");
              break;

            case 0x20:
            case 0x24:
              printf("USB Direct Connection\n");
              break;

            case 0x00:
              printf("WiFi (VEXnet 1.0)\n");
              break;

            case 0x04:
            case 0x34:
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
