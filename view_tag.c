#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "view_tag.h"
#include "edit_tag.h"
#include "types.h"

/* Helper to convert 4-byte syncsafe (used in ID3 header) to int */
static uint syncsafe_to_int(const unsigned char s[4])
{
    return ((s[0] & 0x7F) << 21) |
           ((s[1] & 0x7F) << 14) |
           ((s[2] & 0x7F) << 7) |
           ((s[3] & 0x7F));
}

/* Convert 4-byte big-endian to host uint */
static uint be32_to_uint(const unsigned char b[4])
{
    return ((uint)b[0] << 24) | ((uint)b[1] << 16) | ((uint)b[2] << 8) | (uint)b[3];
}

/* Read ID3 header and all frames in the tag area (v2.3) */
static Status read_id3_tag(FILE *f, ID3Tag *tag)
{
    if (!f || !tag)
        return p_failure;

    if (fseek(f, 0, SEEK_SET) != 0)
        return p_failure;

    if (fread(tag->header, 1, 10, f) != 10)
        return p_failure;

    if (strncmp((char *)tag->header, "ID3", 3) != 0)
        return p_failure;

    unsigned char ver_major = tag->header[3];
    unsigned char ver_rev = tag->header[4];
    /* we currently support v2.3.x */
    if (ver_major != 3)
    {
        /* still continue but warn; many files are 2.3 */
        /* we won't hard-fail, but parsing assumes v2.3 frames */
    }

    unsigned char size_bytes[4];
    memcpy(size_bytes, tag->header + 6, 4);
    tag->tag_size = syncsafe_to_int(size_bytes);

    /* tag_size is size of tag after header; read the tag area */
    unsigned char *tag_buf = malloc(tag->tag_size);
    if (!tag_buf)
        return p_failure;
    if (fread(tag_buf, 1, tag->tag_size, f) != tag->tag_size)
    {
        free(tag_buf);
        return p_failure;
    }

    /* Parse frames: frames start at offset 0 of tag_buf for v2.3 (no extended header handling) */
    size_t offset = 0;
    int capacity = 16;
    tag->frames = calloc(capacity, sizeof(Frame));
    tag->frame_count = 0;
    while (offset + 10 <= tag->tag_size)
    {
        unsigned char *p = tag_buf + offset;
        /* If frame id is zero or non-printable, it's padding -> break */
        if (p[0] == 0)
            break;
        Frame fframe;
        memset(&fframe, 0, sizeof(fframe));
        memcpy(fframe.id, p, 4);
        fframe.id[4] = '\0';
        fframe.size = be32_to_uint(p + 4);
        memcpy(fframe.flags, p + 8, 2);

        /* Sanity check */
        if (fframe.size > tag->tag_size - offset - 10)
        {
            /* malformed or end; stop parsing */
            break;
        }
        fframe.data = malloc(fframe.size);
        if (!fframe.data)
        {
            free(tag_buf);
            return p_failure;
        }
        if (fframe.size > 0)
            memcpy(fframe.data, p + 10, fframe.size);

        /* store */
        if (tag->frame_count >= capacity)
        {
            capacity *= 2;
            Frame *tmp = realloc(tag->frames, capacity * sizeof(Frame));
            if (!tmp)
            {
                free(tag_buf);
                return p_failure;
            }
            tag->frames = tmp;
        }
        tag->frames[tag->frame_count++] = fframe;

        offset += 10 + fframe.size;
    }
    free(tag_buf);
    return p_success;
}

/* Free tag memory */
Status free_id3_tag(ID3Tag *tag)
{
    if (!tag)
        return p_failure;
    for (int i = 0; i < tag->frame_count; ++i)
    {
        free(tag->frames[i].data);
    }
    free(tag->frames);
    tag->frames = NULL;
    tag->frame_count = 0;
    return p_success;
}

/* Find frame pointer by ID */
static Frame *find_frame(ID3Tag *tag, const char *id)
{
    for (int i = 0; i < tag->frame_count; ++i)
    {
        if (strncmp(tag->frames[i].id, id, 4) == 0)
            return &tag->frames[i];
    }
    return NULL;
}

/* Extract text from a text frame: first byte is encoding (0 = ISO-8859-1, 1 = UTF-16) */
static char *extract_text_from_frame(Frame *f)
{
    if (!f || f->size == 0 || !f->data)
        return NULL;
    unsigned char enc = f->data[0];
    size_t text_len = (f->size >= 1) ? (f->size - 1) : 0;
    if (text_len == 0)
        return NULL;

    char *out = malloc(text_len + 1);
    if (!out)
        return NULL;

    if (enc == 0)
    {
        memcpy(out, f->data + 1, text_len);
        out[text_len] = '\0';
    }
    else if (enc == 1)
    {
        /* naive: if BOM present (0xFF 0xFE or 0xFE 0xFF) skip it and try to extract ASCII bytes */
        if (text_len >= 2 && ((unsigned char)f->data[1] == 0xFF || (unsigned char)f->data[1] == 0xFE))
        {
            /* skip BOM, then take every second byte if LE */
            size_t pos = 1;
            size_t j = 0;
            for (; pos + 1 < f->size; pos += 2)
            {
                unsigned char b = f->data[pos + ((f->data[1] == 0xFF) ? 2 : 1)];
                out[j++] = (char)b;
            }
            out[j] = '\0';
        }
        else
        {
            /* fallback: copy bytes, but treat as ASCII */
            memcpy(out, f->data + 1, text_len);
            out[text_len] = '\0';
        }
    }
    else
    {
        /* unknown encoding: copy raw */
        memcpy(out, f->data + 1, text_len);
        out[text_len] = '\0';
    }
    return out;
}

/* Extract comment (COMM) frame: format: enc(1) + lang(3) + shortdesc (term) + text */
static char *extract_comment_from_frame(Frame *f)
{
    if (!f || f->size == 0 || !f->data)
        return NULL;
    unsigned char enc = f->data[0];
    if (f->size <= 4)
        return NULL; /* no space for lang and text */
    size_t pos = 1;
    char lang[4] = {0};
    memcpy(lang, f->data + pos, 3);
    pos += 3;
    /* short description until 0x00 (for encoding 0) */
    size_t desc_start = pos;
    size_t desc_end = desc_start;
    while (desc_end < f->size && f->data[desc_end] != 0x00)
        desc_end++;
    /* after desc_end + 1, the rest is comment text */
    size_t text_start = desc_end + 1;
    if (text_start >= f->size)
        return NULL;
    size_t text_len = f->size - text_start;
    char *out = malloc(text_len + 1);
    if (!out)
        return NULL;
    memcpy(out, f->data + text_start, text_len);
    out[text_len] = '\0';
    return out;
}

/* Print frames in the required order and formatting to match sample output */
Status view_tag(char *argv[], const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        printf("❌ERROR: Unable to Open the %s file.\n", filename);
        printf("➡️INFO: For Viewing the Tags -> ./mp3_tag_reader -v <file_name.mp3>\n");
        return p_failure;
    }

    ID3Tag tag = {0};
    if (read_id3_tag(f, &tag) != p_success)
    {
        printf("❌ERROR: The file Signature is not matching with that of a '.mp3' file.\n");
        fclose(f);
        return p_failure;
    }

    /* Print header info like sample */
    unsigned char ver_major = tag.header[3];
    unsigned char ver_rev = tag.header[4];
    printf("                  MP3 TAG READER & EDITOR                   \n");
    printf("============================================================\n");
    printf("Version ID : %u.%u\n", ver_major, ver_rev);
    printf("------------------------------------------------------------\n");

    /* Extract each frame */
    Frame *f_title = find_frame(&tag, "TIT2");
    Frame *f_artist = find_frame(&tag, "TPE1");
    Frame *f_album = find_frame(&tag, "TALB");
    Frame *f_year = find_frame(&tag, "TYER");
    // Frame *f_track = find_frame(&tag, "TRCK"); /* track sample shows Track */
    Frame *f_genre = find_frame(&tag, "TCON");
    Frame *f_comment = find_frame(&tag, "COMM");

    char *s;
    s = f_title ? extract_text_from_frame(f_title) : NULL;
    printf("Title      : %s\n", s ? s : "");
    free(s);

    s = f_album ? extract_text_from_frame(f_album) : NULL;
    printf("Album      : %s\n", s ? s : "");
    free(s);

    s = f_year ? extract_text_from_frame(f_year) : NULL;
    printf("Year       : %s\n", s ? s : "");
    free(s);

    s = f_genre ? extract_text_from_frame(f_genre) : NULL;
    printf("Genre      : %s\n", s ? s : "");
    free(s);

    s = f_artist ? extract_text_from_frame(f_artist) : NULL;
    printf("Artist     : %s\n", s ? s : "");
    free(s);

    char *cmt = f_comment ? extract_comment_from_frame(f_comment) : NULL;
    printf("Comment    : %s\n\n", cmt ? cmt : "");
    free(cmt);

    printf("Extracting Album Art - Done✅\n");

    free_id3_tag(&tag);
    fclose(f);
    return p_success;
}

/* CLI validation for view */
Status read_and_validate_mp3_file(char *argv[], char *filename_out)
{
    if (argv[2] == NULL)
    {
        printf("➡️INFO: For Viewing the Tags -> ./mp3_tag_reader -v <file_name.mp3>\n");
        return p_failure;
    }
    strncpy(filename_out, argv[2], 1023);
    filename_out[1023] = '\0';
    return p_success;
}

OperationType check_operation(char *argv[])
{
    if (strncmp(argv[1], "-v", 2) == 0)
    {
        return p_view;
    }
    else if (strncmp(argv[1], "-e", 2) == 0)
    {
        return p_edit;
    }
    else if (strncmp(argv[1], "--help", 6) == 0 || strncmp(argv[1], "-h", 2) == 0)
    {
        return p_help;
    }
    else
    {
        return p_unsupported;
    }
}
