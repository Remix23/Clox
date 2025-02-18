#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "memory.h"
#include "value.h"
#include "vm.h"
#include "test.h"


void testerRunner (const char* test_path) {

    DIR* test_dir;
    struct dirent* entry;
    int test_files = 0;

    test_dir = opendir(test_path);
    if (test_dir == NULL) {
        printf("Error: Could not open directory\n");
        exit(1);
    }

    while ( (entry = readdir(test_dir)) ) {
        if (entry -> d_type == DT_DIR) continue;

        const char* dot_pos = strrchr(entry -> d_name, '.');
        if (dot_pos == NULL) continue;

        if (strcmp(dot_pos, ".lox") != 0) continue;
        
        test_files++;
        printf ("File %3d: %s\n", test_files, entry -> d_name);
    }

    closedir(test_dir);
    printf("Total files: %d\n", test_files);
}

