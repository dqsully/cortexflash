#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>

typedef struct {
  int (*open)(void *storage, const char *filename);
  int (*close)(void *storage);
  int (*size)(void *storage);
  int (*read)(void *storage, void *data, unsigned int *len);
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

typedef enum {
  kParserError_none,
  kParserError_system,
  kParserError_invalidFile,
} parserError_t;

parserPackage_t *initParser(parserType_t parserType);

parserError_t hex_open(void *storage, const char *filename);
int hex_close(void *storage);
int hex_size(void *storage);
int hex_read(void *storage, void *data, unsigned int *len);
parserError_t bin_open(void *storage, const char *filename);
int bin_close(void *storage);
int bin_size(void *storage);
int bin_read(void *storage, void *data, unsigned int *len);

static parser_t hexParser = {hex_open, hex_close, hex_size, hex_read};
static parser_t binParser = {bin_open, bin_close, bin_size, bin_read};
