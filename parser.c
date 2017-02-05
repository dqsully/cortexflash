#include "parser.h"

static parser_t hexParser = {hex_open, hex_close, hex_size, hex_read};
static parser_t binParser = {bin_open, bin_close, bin_size, bin_read};

parserPackage_t initParser(parserType_t parserType) {
  parserPackage_t ret = {0};

  switch(parserType) {
    case kStorageType_hex:
      ret.parser = &hexParser;
      ret.storage = calloc(sizeof(hexStorage_t), 1);
      return ret;
    case kStorageType_bin:
      ret.parser = &binParser;
      ret.storage = calloc(sizeof(binStorage_t), 1);
      return ret;
  }

  return ret;
}

parserError_t hex_open(void *storage, const char *filename) {
  hexStorage_t *st = storage;
  char mark, buffer[9];
  int i, fd, e;
  uint8_t checksum;
  uint32_t base = 0;
  unsigned int c, last_address = 0x0;

  unsigned int reclen, address, type;
  uint8_t *record;

  fd = open(filename, O_RDONLY);
  if(fd < 0)
    return kParserError_system;

  // Read in the file
  while(read(fd, &mark, 1) != 0) {
    // Skip newline characters
    if(mark == '\n' || mark == '\r')
      continue;

    // Make sure line starts with ':'
    if(mark != ':')
      goto eBadFile;

    // Get the reclen, address, and type
    buffer[8] = 0;
    e = read(fd, &buffer, 8);
    // Make sure 8 bytes are read
    if(e != 8)
      goto eBadFile;
    e = sscanf(buffer, "%2x%4x%2x", &reclen, &address, &type);
    // Make sure 3 variables were read
    if(e != 3)
      goto eBadFile;

    // Setup the checksum
    checksum = reclen + ((address & 0xff00) >> 8) + ((address & 0x00ff) >> 0) + type;

    switch(type) {
      // Data record
      case 0:
        c = address - last_address;
        st->data = realloc(st->data, st->data_len + c + reclen);

        // If there is a gap, set it to 0xff and increment the length
        if(c > 0) {
          memset(&st->data[st->data_len], 0xff, c);
          st->data_len += c;
        }

        last_address = address + reclen;
        record = &st->data[st->data_len];
        st->data_len += reclen;
        break;

      // Extended segment address record
      case 2:
        base = 0;
        break;

      // Extended linear address recorc
      case 4:
        base = address;
        break;
    }

    // Force end of string
    buffer[2] = 0;

    for(i=0; i<reclen; i++) {
      e = read(fd, &buffer, 2);
      if(e != 2)
        goto eBadFile;

      e = sscanf(buffer, "%2x", &c);
      if(e != 1)
        goto eBadFile;

      // Add the byte to the checksum
      checksum += c;

      switch(type) {
        case 0:
          record[i] = c;
        break;

        case 2:
        case 4:
          base = (base << 8) | c;
        break;
      }
    }

    e = read(fd, &buffer, 2);
    if(e != 2)
      goto eBadFile;
    e = sscanf(buffer, "%2x", &c);
    if(e != 1)
      goto eBadFile;
    if((uint8_t)(checksum + c) != 0x00)
      goto eBadFile;

    switch(type) {
      // End of File
      case 1:
        close(fd);
        return kParserError_none;

      case 2:
        base = base << 4;
      case 4:
        base = be_u32(base);
        // Reset last_address since our base changed
        last_address = 0;

        if(st->base == 0) {
          st->base = base;
          break;
        }

        // We can't cope with files out of order
        if(base < st->base)
          goto eBadFile;

        // if there is a gap, enlarge and fill with zeros
        unsigned int len = base - st->base;
        if(len > st->data_len) {
          st->data = realloc(st->data, len);
          memset(&st->data[st->data_len], 0, len - st->data_len);
          st->data_len = len;
        }
        break;
    }
  }

  close(fd);
  return kParserError_none;

eBadFile:
  close(fd);
  return kParserError_invalidFile;
}
parserError_t hex_close(void *storage) {
  hexStorage_t *st = storage;
  if(st)
    free(st->data);
  free(st);
  return kParserError_none;
}
parserError_t hex_size(void *storage) {
  hexStorage_t *st = storage;
  return st->data_len;
}
parserError_t hex_read(void *storage, void *data, size_t offset, size_t *len) {
  hexStorage_t *st = storage;
  size_t get = st->data_len - st->offset;
  get = get > *len ? *len : get;

  if(offset > st->data_len)
    return kParserError_system;

  memcpy(data, &st->data[offset], get);
  *len = get;

  return kParserError_none;
}

parserError_t bin_open(void *storage, const char *filename) {
  return kParserError_system;
}
parserError_t bin_close(void *storage) {
  return kParserError_system;
}
parserError_t bin_size(void *storage) {
  return kParserError_system;
}
parserError_t bin_read(void *storage, void *data, size_t offset, size_t *len) {
  return kParserError_system;
}
