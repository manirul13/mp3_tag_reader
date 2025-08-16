#ifndef VIEW_H
#define VIEW_H

#include "types.h"
#include <stdio.h>

typedef struct _Frame {
    char id[5];      /* 4 chars + null */
    uint size;       /* frame size (big-endian in file; we'll store host order) */
    unsigned char flags[2];
    unsigned char *data; /* raw frame data (size bytes) */
} Frame;

typedef struct _ID3Tag {
    unsigned char header[10];
    uint tag_size;    /* size from header (syncsafe -> host) */
    Frame *frames;
    int frame_count;
} ID3Tag;

/* Parsing, printing helpers */
Status read_and_validate_mp3_file (char* argv[], char *filename_out);
OperationType check_operation (char* argv[]);
Status view_tag (char* argv[], const char *filename);
Status free_id3_tag(ID3Tag *tag);

#endif




