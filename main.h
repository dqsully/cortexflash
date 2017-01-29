#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "serial.h"
#include "stm32.h"
#include "parser.h"

int main(int argc, char* argv[]);
bool parseOptions(int argc, char* argv[]);
void showHelp(char *programName);
void cleanup();
bool testBootloader();
int init();
bool getSystemStatus();
void enterUserProgram();

struct timespec _time = {0};

#define nSleep(t) _time.tv_nsec = t; nanosleep(&_time, NULL)
#define uSleep(t) nSleep(t * 1000)

typedef struct {
  off_t offset;
  uint8_t len;
  bool clear;
} diff_t;
