#ifndef MP3EDIT_H
#define MP3EDIT_H

#include "types.h"
#include <stdio.h>

typedef struct _TagData
{
    FILE* fptr_mp3;
    char frame_Id [5];
    char frame_Id_value [256];
    uint frame_Id_size;
} TagData;

/* Function prototypes */
Status read_and_validate_mp3_file_args (char* argv[], TagData* mp3tagData);
Status edit_tag (char* argv[], TagData* mp3tagData);

#endif




