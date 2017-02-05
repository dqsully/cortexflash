#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>

#include "serial.h"
#include "stm32.h"
#include "parser.h"

void beginTimer();
double endTimer();
int main(int argc, char* argv[]);
bool parseOptions(int argc, char* argv[]);
void showHelp(char *programName);
void cleanup();
bool testBootloader();
int init();
bool getSystemStatus();
void enterUserProgram();

struct timespec _time = {0};
struct timeval startTime, endTime;

#define nSleep(t) _time.tv_nsec = t; nanosleep(&_time, NULL)
#define uSleep(t) nSleep(t * 1000)

typedef struct {
  off_t offset;
  short len;
  bool clear;
} diff_t;
