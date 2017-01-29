#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include "utils.h"

typedef enum {
  kParserError_none,
  kParserError_system,
  kParserError_invalidFile,
} parserError_t;

typedef struct {
  parserError_t (*open)(void *storage, const char *filename);
  parserError_t (*close)(void *storage);
  parserError_t (*size)(void *storage);
  parserError_t (*read)(void *storage, void *data, size_t offset, size_t *len);
} parser_t;
typedef struct {
  parser_t *parser;
  void *storage;
} parserPackage_t;
typedef enum {
  kStorageType_hex,
  kStorageType_bin
} parserType_t;

typedef struct {
  size_t data_len, offset;
  uint8_t *data, base;
} hexStorage_t;
typedef struct {
  int fd;
  char write;
  struct stat stat;
} binStorage_t;

parserPackage_t initParser(parserType_t parserType);

parserError_t hex_open(void *storage, const char *filename);
parserError_t hex_close(void *storage);
parserError_t hex_size(void *storage);
parserError_t hex_read(void *storage, void *data, size_t offset, size_t *len);
parserError_t bin_open(void *storage, const char *filename);
parserError_t bin_close(void *storage);
parserError_t bin_size(void *storage);
parserError_t bin_read(void *storage, void *data, size_t offset, size_t *len);
