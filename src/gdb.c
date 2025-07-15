#include "gdb.h"
#include "types.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pty.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <pthread.h>
#include <termios.h>

#include <stdatomic.h>

static pthread_t gdb_thread;

static pthread_mutex_t mutex;
static pthread_cond_t cv;
static gdb_command_t gdb_command;
static uint32_t handle = 0;

static pthread_cond_t out_cv;
static gdb_output_t gdb_output = {};



void gdb_wait_until_finished() {
    pthread_join(gdb_thread, NULL);
    wait(NULL); // wait on child process to finish.
}

static bool search_for_tag(uint8_t* buffer, uint32_t length, literal tag) {
    //
    // @Incomplete: Very dumb implementation, we can do better...
    //

    const char* data = (const char*) buffer;
    for (size_t i = 0; i < length; i++) {

        if (strncmp(data, tag.data, tag.count) == 0) {
            return true;
        }

        data += 1;
    }

    return false;
}

static void read_from_gdb_instance(int fd, uint8_t* buffer, uint32_t size, int32_t* num_read) {
    while (1) {
        *num_read = read(fd, buffer, sizeof(buffer));
        if (*num_read <= 0) {
            return; // @ReportError:
        }

        if (write(STDOUT_FILENO, buffer, *num_read) != *num_read) {
            puts("partial/failed write (STDOUT_FILENO)");
            return; // @ReportError:
        }

        bool found = search_for_tag(buffer, *num_read, lit("(gdb)"));
        if (found) {
            break;
        }
    }
}

static literal get_message_from_gdb_command(gdb_command_t command) {
    switch (command.type) {
    case GDB_COMMAND_TYPE_QUIT:      return lit("-gdb-exit");
    case GDB_COMMAND_TYPE_PWD:       return lit("-environment-pwd");
    case GDB_COMMAND_TYPE_LOAD_FILE: return lit("-file-exec-and-symbols"); // expects filename.
    default:         assert(0);      return lit("");
    }
}

static void* process(void* arg) {
    int* fd = arg;

    int flags = fcntl(*fd, F_GETFL);
    if (flags != -1) {
        fcntl(*fd, F_SETFL, /*O_DIRECT | O_NONBLOCK | */flags); // @Speed: measure latency with O_DIRECT and O_NONBLOCK.
    }

    struct termios termios_flags;
    tcgetattr(*fd, &termios_flags);

    termios_flags.c_iflag &= ~(IGNPAR | INPCK | INLCR | IGNCR | ICRNL | IXON | IXOFF | ISTRIP);
    termios_flags.c_iflag |= IGNBRK | BRKINT | IMAXBEL | IXANY;
    termios_flags.c_oflag &= ~OPOST;
    termios_flags.c_cflag &= ~(CSTOPB | CREAD | PARENB | HUPCL);
    termios_flags.c_cflag |= CS8 | CLOCAL;
    termios_flags.c_cc[VMIN] = 0;
    // termios_flags.c_cc[VTIME] = 1; // @Speed: VTIME allows us to block for n deciseconds.

    termios_flags.c_lflag &= ~(ECHOE | ECHO | ECHONL | ISIG | ICANON | IEXTEN | NOFLSH | TOSTOP);
    cfsetospeed(&termios_flags, __MAX_BAUD);
    tcsetattr(*fd, TCSANOW, &termios_flags);


    fd_set set;
    FD_ZERO(&set);
    FD_SET(*fd, &set);
    select(*fd+1, &set, NULL, NULL, NULL);

    int num_read = 0;
    uint8_t buffer[4096] = {};
    read_from_gdb_instance(*fd, buffer, sizeof(buffer), &num_read); // @Note: skip gdb version.

    while (true) {

        gdb_command_t command = {};
        {
            pthread_mutex_lock(&mutex);

            while (handle == 0) {
                pthread_cond_wait(&cv, &mutex);
            }

            command = gdb_command;

            pthread_mutex_unlock(&mutex);
        }

        literal message = get_message_from_gdb_command(command);

        if (write(*fd, message.data, message.count) != (ssize_t)message.count) {
            puts("partial/failed write (masterFd)");
            return NULL;
        }

        if (command.type == GDB_COMMAND_TYPE_QUIT) {
            break;
        }

        FD_ZERO(&set);
        FD_SET(*fd, &set);
        select(*fd+1, &set, NULL, NULL, NULL);

        if (FD_ISSET(*fd, &set)) {

            num_read = 0;
            memset(buffer, 0, sizeof(buffer));
            read_from_gdb_instance(*fd, buffer, sizeof(buffer), &num_read);

            // @Incomplete: actually parse the thing...
            gdb_output_t parsed = {
                .type = GDB_COMMAND_TYPE_PWD,
                .cwd = lit(" hello world from gdb! "),
            };

            {
                pthread_mutex_lock(&mutex);

                if (handle) {
                    gdb_output = parsed;
                    handle = 0;
                    pthread_cond_signal(&out_cv);
                }

                pthread_mutex_unlock(&mutex);
            }
        }
    }

    return NULL;
}

void gdb_start_instance() {
    static int master_fd = 0;
    pid_t pid = forkpty(&master_fd, NULL, NULL, NULL);
    if (pid == 0) {

        execlp("gdb", "gdb", "--interpreter=mi4", NULL);
        exit(0);

    } else {
        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&cv, NULL);
        pthread_cond_init(&out_cv, NULL);

        pthread_create(&gdb_thread, NULL, process, &master_fd);
    }
}

uint32_t gdb_send_command(gdb_command_t command) {
    pthread_mutex_lock(&mutex);
    assert(handle == 0);                    // @Note: can't issue another command, while the result of the last one has not been consumed...

    gdb_command = command;
    handle = 1;

    pthread_cond_signal(&cv);
    pthread_mutex_unlock(&mutex);

    return 0;
}

gdb_output_t gdb_wait_command_result(uint32_t unused) {
    gdb_output_t result = {};

    {
        pthread_mutex_lock(&mutex);

        while (handle) {
            pthread_cond_wait(&out_cv, &mutex);
        }

        result = gdb_output;

        pthread_mutex_unlock(&mutex);
    }

    return result;
}

//
// GDB: load executable, set breakpoints, list stacks, locals, list all threads, etc.
// GDB: parse output commands.
// Abstract everything away so that we are able to update it from the UI.
//
// UI: load file, set breakpoints, buttons, windows, layouts.
// UI: create window, create some text, create some buttons.
//

#if 0
"-break-after" //"ignore"
"-break-commands"
"-break-condition" //"cond"
"-break-delete" //"delete breakpoint"
"-break-disable" //"disable breakpoint"
"-break-enable" //"enable breakpoint"
"-break-info" //"info break"
"-break-insert -f"
"-break-list" //"info break"
"-break-watch"
"-data-disassemble"
"-data-evaluate-expression"
"-data-list-changed-registers"
"-data-list-register-names"
"-data-list-register-values"
"-data-read-memory"
"-data-write-memory"
"-data-write-register-values"
"-enable-pretty-printing"
"-enable-timings"
"-environment-cd"
"-environment-directory"
"-environment-path"
"-environment-pwd"
"-exec-abort"
"-exec-arguments" //"set args"
"-exec-continue"
"-exec-finish"
"-exec-interrupt"
"-exec-next"
"-exec-next-instruction"
"-exec-run"
"-exec-step"
"-exec-step-instruction"
"-exec-until"
"-file-exec-and-symbols" //"file"
"-file-exec-file" //"exec-file"
"-file-list-exec-source-file"
"-file-list-exec-source-files"
"-file-symbol-file" //"symbol-file"
"-gdb-exit"
"-gdb-set" //"set"
"-gdb-show" //"show"
"-gdb-version" //"show version"
"-inferior-tty-set"
"-inferior-tty-show"
"-interpreter-exec"
"-list-features"
"handle"
// "-signal-handle"
"-stack-info-depth"
"-stack-info-frame"
"-stack-list-arguments"
"-stack-list-frames"
"-stack-list-locals"
"-stack-select-frame"
"-symbol-list-lines"
"-target-attach"
"-target-detach" //"detach"
"-target-disconnect" //"disconnect"
"-target-download"
"-target-select"
"-thread-info"
"-thread-list-ids"
"-thread-select"
"-trace-find"
"-trace-start"
"-trace-stop"
"-var-assign"
"-var-create"
"-var-delete"
"-var-evaluate-expression"
"-var-info-path-expression"
"-var-info-num-children"
"-var-info-type"
"-var-list-children"
"-var-set-format"
"-var-set-frozen"
"-var-show-attributes"
"-var-show-format"
"-var-update"
#endif

