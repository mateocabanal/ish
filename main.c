#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/calls.h"
#include "kernel/init.h"
#include "kernel/task.h"
#include "xX_main_Xx.h"

int main(int argc, char *const argv[]) {
    // do_execve expects envp as Linux-style NUL-separated strings ending in an
    // extra NUL. For the command-line runner we only preserve TERM, which is
    // enough for shells and terminal programs started by the test binary.
    char envp[100] = {0};
    if (getenv("TERM"))
        strcpy(envp, getenv("TERM") - strlen("TERM") - 1);
    int err = xX_main_Xx(argc, argv, envp);
    if (err < 0) {
        fprintf(stderr, "xX_main_Xx: %s\n", strerror(-err));
        return err;
    }
    create_some_device_nodes();
    do_mount(&procfs, "proc", "/proc", "", 0);
    do_mount(&devptsfs, "devpts", "/dev/pts", "", 0);
    task_run_current();
}
