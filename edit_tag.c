#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "edit_tag.h"
#include "view_tag.h" 
#include "types.h"   /* for ID3 tag structures and helpers */


/* helpers (some duplicate small helpers from mp3view.c) */
static uint syncsafe_to_int(const unsigned char s[4])
{
    return ((s[0] & 0x7F) << 21) |
           ((s[1] & 0x7F) << 14) |
           ((s[2] & 0x7F) << 7) |
           ((s[3] & 0x7F));
}

static void int_to_syncsafe(uint val, unsigned char out[4])
{
    out[0] = (val >> 21) & 0x7F;
    out[1] = (val >> 14) & 0x7F;
    out[2] = (val >> 7) & 0x7F;
    out[3] = val & 0x7F;
}

static uint be32_to_uint(const unsigned char b[4])
{
    return ((uint)b[0] << 24) | ((uint)b[1] << 16) | ((uint)b[2] << 8) | (uint)b[3];
}

static void uint_to_be32(uint v, unsigned char out[4])
{
    out[0] = (v >> 24) & 0xFF;
    out[1] = (v >> 16) & 0xFF;
    out[2] = (v >> 8) & 0xFF;
    out[3] = v & 0xFF;
}

/* Validate args and fill TagData */
Status read_and_validate_mp3_file_args(char *argv[], TagData *mp3tagData)
{
    if (!mp3tagData)
        return p_failure;
    if (argv[2] == NULL)
    {
        printf("INFO: For Editing the Tags -> ./mp3_tag_reader -e <modifier> \"New_Value\" <file_name.mp3>\n");
        printf("INFO: Modifier Functions:\n");
        printf("-t\tModify Title Tag\n-A\tModify Artist Tag\n-a\tModify Album Tag\n-y\tModify Year Tag\n-G\tModify Content Type Tag\n-c\tModify Comments Tag\n");
        return p_failure;
    }

    if ((strncmp(argv[2], "-t", 2) == 0))
        strncpy(mp3tagData->frame_Id, "TIT2", 5);
    else if ((strncmp(argv[2], "-A", 2) == 0))
        strncpy(mp3tagData->frame_Id, "TPE1", 5);
    else if ((strncmp(argv[2], "-a", 2) == 0))
        strncpy(mp3tagData->frame_Id, "TALB", 5);
    else if ((strncmp(argv[2], "-y", 2) == 0))
        strncpy(mp3tagData->frame_Id, "TYER", 5);
    else if ((strncmp(argv[2], "-G", 2) == 0))
        strncpy(mp3tagData->frame_Id, "TCON", 5);
    else if ((strncmp(argv[2], "-c", 2) == 0))
        strncpy(mp3tagData->frame_Id, "COMM", 5);
    else
    {
        printf("❌ERROR: Unsupported Modifier.\n");
        return p_failure;
    }

    if (argv[3] == NULL)
    {
        printf("❌ERROR: New_Value to be updated on the Frame ID %s is Empty.\n", mp3tagData->frame_Id);
        return p_failure;
    }
    size_t inlen = strlen(argv[3]);
    if (inlen + 1 >= sizeof(mp3tagData->frame_Id_value))
    {
        printf("❌ERROR: Length of the Data is too Long!.\n");
        return p_failure;
    }
    strncpy(mp3tagData->frame_Id_value, argv[3], sizeof(mp3tagData->frame_Id_value) - 1);
    mp3tagData->frame_Id_value[sizeof(mp3tagData->frame_Id_value) - 1] = '\0';
    mp3tagData->frame_Id_size = (uint)(inlen + 1); /* we'll use 1 byte for encoding plus text */

    if (argv[4] == NULL)
    {
        printf("➡️INFO: For Editing the Tags -> ./mp3_tag_reader -e <modifier> \"New_Value\" <file_name.mp3>\n");
        return p_failure;
    }
    /* check that file exists and is ID3 */
    FILE *f = fopen(argv[4], "rb");
    if (!f)
    {
        printf("❌ERROR: Unable to Open the %s file.\n", argv[4]);
        return p_failure;
    }
    char sig[4] = {0};
    if (fread(sig, 1, 3, f) != 3 || strncmp(sig, "ID3", 3) != 0)
    {
        printf("❌ERROR: The file Signature is not matching with that of a '.mp3' file.\n");
        fclose(f);
        return p_failure;
    }
    fclose(f);
    return p_success;
}

/* Rebuild tag frames in memory and write back to file (safe rewrite) */
Status edit_tag(char *argv[], TagData *mp3tagData)
{
    const char *filename = argv[4];
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        printf("❌ERROR: Unable to open file.\n");
        return p_failure;
    }

    /* Read old header */
    unsigned char header[10];
    if (fread(header, 1, 10, f) != 10)
    {
        fclose(f);
        return p_failure;
    }
    if (strncmp((char *)header, "ID3", 3) != 0)
    {
        fclose(f);
        return p_failure;
    }

    unsigned char size_bytes[4];
    memcpy(size_bytes, header + 6, 4);
    uint old_tag_size = syncsafe_to_int(size_bytes);

    /* read old tag block */
    unsigned char *tag_block = malloc(old_tag_size);
    if (!tag_block)
    {
        fclose(f);
        return p_failure;
    }
    if (fread(tag_block, 1, old_tag_size, f) != old_tag_size)
    {
        free(tag_block);
        fclose(f);
        return p_failure;
    }

    /* parse old frames into array (like mp3view does) */
    size_t offset = 0;
    typedef struct
    {
        char id[5];
        uint size;
        unsigned char flags[2];
        unsigned char *data;
    } TempFrame;
    TempFrame *frames = NULL;
    int fcount = 0, fcap = 0;
    while (offset + 10 <= old_tag_size)
    {
        unsigned char *p = tag_block + offset;
        if (p[0] == 0)
            break;
        TempFrame tf = {0};
        memcpy(tf.id, p, 4);
        tf.id[4] = '\0';
        tf.size = be32_to_uint(p + 4);
        tf.flags[0] = p[8];
        tf.flags[1] = p[9];
        if (tf.size > old_tag_size - offset - 10)
            break;
        tf.data = malloc(tf.size);
        if (!tf.data)
            break;
        if (tf.size > 0)
            memcpy(tf.data, p + 10, tf.size);
        if (fcount >= fcap)
        {
            fcap = fcap ? fcap * 2 : 16;
            TempFrame *tmp = realloc(frames, fcap * sizeof(TempFrame));
            if (!tmp)
            {
                free(tf.data);
                break;
            }
            frames = tmp;
        }
        frames[fcount++] = tf;
        offset += 10 + tf.size;
    }

    /* We'll modify the target frame or add it if not present */
    int target_index = -1;
    for (int i = 0; i < fcount; ++i)
    {
        if (strncmp(frames[i].id, mp3tagData->frame_Id, 4) == 0)
        {
            target_index = i;
            break;
        }
    }

    /* Build new frame data for the updated value.
       For text frames: content = [encoding byte=0] + value bytes
       For COMM: content = [encoding=0] + language(3) + shortdesc('\0') + comment text
    */
    unsigned char *new_frame_data = NULL;
    uint new_frame_size = 0;
    if (strncmp(mp3tagData->frame_Id, "COMM", 4) == 0)
    {
        /* language 'eng', shortdesc empty */
        const char *lang = "eng";
        const char *comment = mp3tagData->frame_Id_value;
        uint comment_len = (uint)strlen(comment);
        new_frame_size = 1 + 3 + 1 + comment_len; /* enc + lang + empty desc + comment */
        new_frame_data = malloc(new_frame_size);
        if (!new_frame_data)
        { /* cleanup and exit */
        }
        new_frame_data[0] = 0; /* encoding ISO-8859-1 */
        memcpy(new_frame_data + 1, lang, 3);
        new_frame_data[4] = 0; /* empty shortdesc terminated */
        memcpy(new_frame_data + 5, comment, comment_len);
    }
    else
    {
        const char *val = mp3tagData->frame_Id_value;
        uint vlen = (uint)strlen(val);
        new_frame_size = 1 + vlen;
        new_frame_data = malloc(new_frame_size);
        if (!new_frame_data)
        { /* cleanup later */
        }
        new_frame_data[0] = 0; /* encoding ISO-8859-1 */
        memcpy(new_frame_data + 1, val, vlen);
    }

    /* If target exists, free its data and replace, else append a new frame */
    if (target_index >= 0)
    {
        free(frames[target_index].data);
        frames[target_index].data = new_frame_data;
        frames[target_index].size = new_frame_size;
    }
    else
    {
        /* append new frame */
        if (fcount >= fcap)
        {
            fcap = fcap ? fcap * 2 : 16;
            TempFrame *tmp = realloc(frames, fcap * sizeof(TempFrame));
            if (!tmp)
            { /* cleanup */
            }
            frames = tmp;
        }
        TempFrame tf = {0};
        strncpy(tf.id, mp3tagData->frame_Id, 4);
        tf.data = new_frame_data;
        tf.size = new_frame_size;
        tf.flags[0] = tf.flags[1] = 0;
        frames[fcount++] = tf;
    }

    /* Rebuild new tag bytes from frames */
    /* Calculate total frames bytes */
    uint new_frames_bytes = 0;
    for (int i = 0; i < fcount; ++i)
    {
        new_frames_bytes += 10 + frames[i].size;
    }

    /* New tag size (excluding header) */
    uint new_tag_size = new_frames_bytes;

    /* Create new file temp, write header with updated syncsafe size and write frames, then copy audio data */
    FILE *temp = fopen("temp.mp3", "wb");
    if (!temp)
    {
        printf("❌ERROR: Unable to open temp file.\n"); /* cleanup */
        fclose(f);
        return p_failure;
    }

    /* write header same as old but update size syncsafe */
    unsigned char new_header[10];
    memcpy(new_header, header, 10);
    unsigned char new_size_bytes[4];
    int_to_syncsafe(new_tag_size, new_size_bytes);
    memcpy(new_header + 6, new_size_bytes, 4);
    if (fwrite(new_header, 1, 10, temp) != 10)
    {
        fclose(f);
        fclose(temp);
        return p_failure;
    }

    /* write frames */
    for (int i = 0; i < fcount; ++i)
    {
        /* frame id 4 bytes */
        if (fwrite(frames[i].id, 1, 4, temp) != 4)
        {
            fclose(f);
            fclose(temp);
            return p_failure;
        }
        /* frame size big-endian 4 bytes */
        unsigned char size_be[4];
        uint_to_be32(frames[i].size, size_be);
        if (fwrite(size_be, 1, 4, temp) != 4)
        {
            fclose(f);
            fclose(temp);
            return p_failure;
        }
        /* flags 2 bytes */
        if (fwrite(frames[i].flags, 1, 2, temp) != 2)
        {
            fclose(f);
            fclose(temp);
            return p_failure;
        }
        /* data */
        if (frames[i].size > 0)
        {
            if (fwrite(frames[i].data, 1, frames[i].size, temp) != frames[i].size)
            {
                fclose(f);
                fclose(temp);
                return p_failure;
            }
        }
    }

    /* After frames, there may be padding in old tag area; we will ignore old padding and now copy the rest of file (audio) */
    /* The original file pointer f currently sits after reading header+old tag. We'll seek to header+old_tag_size+10 */
    long audio_offset = 10 + old_tag_size;
    if (fseek(f, audio_offset, SEEK_SET) != 0)
    {
        fclose(f);
        fclose(temp);
        return p_failure;
    }

    /* copy rest of audio */
    char buf[4096];
    size_t rn;
    while ((rn = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        if (fwrite(buf, 1, rn, temp) != rn)
        {
            fclose(f);
            fclose(temp);
            return p_failure;
        }
    }

    fclose(f);
    fclose(temp);

    /* overwrite original file with temp (simple remove+rename) */
    if (remove(filename) != 0)
    {
        /* fallback: try to open original for write and write temp into it */
        printf("⚠️WARNING: unable to remove original file. Attempting overwrite.\n");
    }
    if (rename("temp.mp3", filename) != 0)
    {
        printf("❌ERROR: Unable to replace original file with temp file.\n");
        return p_failure;
    }

    /* Free frames */
    for (int i = 0; i < fcount; ++i)
    {
        free(frames[i].data);
    }
    free(frames);
    free(tag_block);

    /* Print success message like sample */
    if (strncmp(mp3tagData->frame_Id, "TIT2", 4) == 0)
        printf("Title Modification - Done✅\n");
    else if (strncmp(mp3tagData->frame_Id, "TPE1", 4) == 0)
        printf("Artist Modification - Done✅\n");
    else
        printf("Modification - Done✅\n");

    return p_success;
}
