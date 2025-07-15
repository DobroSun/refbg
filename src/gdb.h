#pragma once

#include "types.h"


typedef enum {
    GDB_COMMAND_TYPE_NONE = 0,
    GDB_COMMAND_TYPE_QUIT,
    GDB_COMMAND_TYPE_PWD,

    GDB_COMMAND_TYPE_LOAD_FILE,

} gdb_command_type_t;

typedef struct {
    gdb_command_type_t type;

    union {
        literal string;
    };

} gdb_command_t;

typedef struct {
    gdb_command_type_t type;

    union {
        literal cwd;
    };

} gdb_output_t;

void gdb_start_instance();
void gdb_wait_until_finished();

uint32_t gdb_send_command(gdb_command_t command);

gdb_output_t gdb_wait_command_result(uint32_t handle);

