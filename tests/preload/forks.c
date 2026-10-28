/*
 * forks.c - fork while other threads are inside malloc.              (GIVEN)
 * Run under LD_PRELOAD=build/libmm.so. Four threads allocate and free constantly while the main
 * thread forks 200 children; each child must be able to malloc and exit. Without fork handlers
 * a child sometimes inherits a locked allocator and hangs (grade.py kills it after a timeout).
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile int stop;

static void *churn(void *arg)
{
    (void)arg;
    while (!stop) {
        void *p[32];
        for (int i = 0; i < 32; i++)
            p[i] = malloc(16 + 8 * i);
        for (int i = 0; i < 32; i++)
            free(p[i]);
    }
    return NULL;
}

int main(void)
{
    pthread_t th[4];
    for (int i = 0; i < 4; i++)
        pthread_create(&th[i], NULL, churn, NULL);
    int bad = 0;
    for (int i = 0; i < 200; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char *s = malloc(100);
            if (!s) _exit(2);
            strcpy(s, "child");
            free(s);
            _exit(0);
        }
        int status;
        waitpid(pid, &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            bad++;
    }
    stop = 1;
    for (int i = 0; i < 4; i++)
        pthread_join(th[i], NULL);
    printf("%s\n", bad ? "FAIL" : "OK");
    return bad != 0;
}
