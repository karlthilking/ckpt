#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define NTHREADS        4
#define NPROCESSES      2

void *thread_doit(void *arg)
{
        int             id;
        pid_t           pid;
        uintptr_t       self;
        
        id = (int)(uintptr_t)arg;
        const char fmt[] = "(Thread #%d) pid: %d, "
                           "pthread_self: 0x%lx, iteration: %d\n";

        for (int i = 0; i < 25; i++) {
                sleep(2);
                pid = getpid();
                self = (uintptr_t)pthread_self();
                printf(fmt, id, pid, self, i);
                sleep(2);
        }

        pthread_exit(NULL);
}

void work(void)
{
        pthread_t threads[NTHREADS];

        for (int i = 0; i < NTHREADS; i++) {
                sleep(rand() % 4);
                void *id = (void *)(uintptr_t)i;
                pthread_create(threads + i, NULL, thread_doit, id);
        }

        for (int i = 0; i < NTHREADS; i++) {
                pthread_join(threads[i], NULL);
        }

        exit(0);
}

pid_t create_process(void)
{
        pid_t pid;

        switch ((pid = fork())) {
        case -1:
                perror("fork");
                exit(-1);
        case 0:
                work();
        default:
                break;
        }

        return pid;
}

int main(int argc, char *argv[])
{
        pid_t children[NPROCESSES];

        children[0] = create_process();
        sleep(rand() % 4);
        children[1] = create_process();

        waitpid(children[0], NULL, 0);
        waitpid(children[1], NULL, 0);
        exit(0);
}
