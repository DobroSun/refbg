#include <stdio.h>

int main() {
    int a = 42;
    printf("Hello world 1 := %d\n", a);

    float b = 1.0f;
    printf("Hello world 2 := %f\n", b);

    const char* text = "Text!";
    printf("Hello world 3 := %s\n", text);
    return 0;
}

#if 0
    while (true) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        FD_SET(master_fd, &set);

        if (select(master_fd + 1, &set, NULL, NULL, NULL) == -1) {
            break;
        }

        int num = 0;
        char buffer[4096];

        if (FD_ISSET(STDIN_FILENO, &set)) {
            num = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (num <= 0) {
                exit(EXIT_SUCCESS);
            }

            if (write(master_fd, buffer, num) != num) {
                break;
            }
        }

        if (FD_ISSET(master_fd, &set)) {
            num = read(master_fd, buffer, sizeof(buffer));
            if (num <= 0) {
                exit(EXIT_SUCCESS);
            }

            if (write(STDOUT_FILENO, buffer, num) != num) {
                break;
            }
        }
    }
#endif
