#include <stdio.h>
#include <string.h>
#include "types.h"
#include "view_tag.h"
#include "edit_tag.h"

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("============================================================\n");
        printf("❌ERROR: Incorrect format of Command Line Arguments.\n");
        printf("Usage \"./mp3_tag_reader --help\" for help\n");
        printf("============================================================\n");
        return 0;
    }

    OperationType op = check_operation(argv);
    if (op == p_view)
    {
        printf("============================================================\n");
        char filename[1024] = {0};
        if (read_and_validate_mp3_file(argv, filename) == p_success)
        {
            if (view_tag(argv, filename) == p_success)
            {
                printf("INFO: Done.✅\n");
                printf("============================================================\n");
            }
        }
    }
    else if (op == p_edit)
    {
        printf("                  MP3 TAG READER & EDITOR                   \n");
        printf("============================================================\n");
        TagData td = {0};
        if (read_and_validate_mp3_file_args(argv, &td) == p_success)
        {
            if (edit_tag(argv, &td) == p_success)
            {
                printf("INFO: Done.✅\n");
                printf("============================================================\n");
            }
        }
    }
    else if (op == p_help)
    {
        printf("Help menu for Mp3 Tag Reader and Editor:⤵️\n");
        printf("For viewing the tags - ./mp3_tag_reader -v <filename.mp3>\n");
        printf("For editing the tags - ./mp3_tag_reader -e <modifier> \"New_Value\" <file_name.mp3>\n");
        printf("Modifier Function⤵️\n");
        printf("-t    Modify Title Tag\n");
        // printf("-T    Modify Track Tag\n");
        printf("-a    Modify Artist Tag\n");
        printf("-A    Modify Album Tag\n");
        printf("-y    Modify Year Tag\n");
        printf("-c    Modify Comment Tag\n");
        printf("-g    Modify Genre Tag\n");
    }
    else
    {
        printf("❌ERROR: Unsupported Operation.\n");
        printf("INFO: Use \"./mp3_tag_reader --help\" for Help menu.\n");
    }

    return 0;
}
