/* ckptrepl.c */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <assert.h>
#include <signal.h>
#include "types.h"

static const char *helpmsg =
"OVERVIEW: MacOS Checkpoint-Restart REPL\n\n"
"USAGE: <sub-command> <args> ...\n\n"
"OPTIONS:\n"
"  p <ckpt-file>        Print checkpoint file\n"
"  c <binary> <args>    Execute binary injected with libckpt.dylib\n"
"  r <ckpt-file>        Restart from a saved checkpoint file\n"
"  h                    Display this help message\n"
"  q                    Exit the interactive C/R REPL\n";

static const char *ckpt_helpmsg = 
"     OVERVIEW: Checkpoint Command\n"
"        USAGE: c <binary> <args> ...\n"
"  DESCRIPTION: Execute a binary inject with libckpt\n";

static const char *restart_helpmsg =
"     OVERVIEW: Restart Command\n"
"        USAGE: r <ckpt-file>\n"
"  DESCRIPTION: Restart from a saved checkpoint file\n";

static const char *print_helpmsg =
"     OVERVIEW: Print Checkpoint Command\n"
"        USAGE: p <ckpt-file>\n"
"  DESCRIPTION: Print a saved checkpoint file\n";

extern void getpath(const char *, char *);
extern void print(char *);
extern void checkpoint(char **);
extern void restart(char *);

static pid_t child = -1;

void interactive_usage(void)
{
        dprintf(STDERR_FILENO, "\n%s\n", helpmsg);
}

void ckptcmd_usage(void)
{
        dprintf(STDERR_FILENO, "\n%s\n", ckpt_helpmsg);
}

void restartcmd_usage(void)
{
        dprintf(STDERR_FILENO, "\n%s\n", restart_helpmsg);
}

void printcmd_usage(void)
{
        dprintf(STDERR_FILENO, "\n%s\n", print_helpmsg);
}

void interrupt(int sig)
{
        if (sig != SIGINT || child == -1) {
                return;
        }
        
        printf("\n");
        kill(child, SIGINT);
}

void getargv(char *argstr, char **argv)
{
        char    *tok;
        int     argc = 0;

        if ((tok = strtok(argstr, " ")) == NULL) {
                goto done;
        }

        if ((tok = strtok(NULL, " ")) == NULL) {
                goto done;
        }

        while (tok) {
                argv[argc] = strdup(tok);
                argc++;
                tok = strtok(NULL, " ");
        }

done:
        argv[argc] = NULL;
}

void ckptcmd(char *cmd)
{
        char **argv;

        assert(cmd[0] == 'c');
        if (strlen(cmd) <= 2 || cmd[2] == '\n') {
                ckptcmd_usage();
                return;
        }
        
        argv = malloc(sizeof(char *) * 20);
        getargv(cmd, argv);
        
        child = fork();
        switch (child) {
        case -1:
                warn("fork");
                break;
        case 0:
                signal(SIGINT, SIG_DFL);
                checkpoint(argv);
                unreachable();
        default:
                break;
        }
        
        for (int i = 0; argv[i] != NULL; i++) {
                free(argv[i]);
        }

        free(argv);
        waitpid(child, NULL, 0);
}

void restartcmd(char *cmd)
{
        char *ckptfile;
        
        assert(cmd[0] == 'r');
        if (strlen(cmd) <= 2 || cmd[2] == '\n') {
                restartcmd_usage();
                return;
        }

        ckptfile = cmd + 2;
        
        child = fork();
        switch (child) {
        case -1:
                warn("fork");
                break;
        case 0:
                signal(SIGINT, SIG_DFL);
                restart(ckptfile);
                unreachable();
        default:
                break;
        }

        waitpid(child, NULL, 0);
}

void printcmd(char *cmd)
{
        char *ckptfile;
        
        assert(cmd[0] == 'p');
        if (strlen(cmd) <= 2 || cmd[2] == '\n') {
                printcmd_usage();
                return;
        }

        ckptfile = cmd + 2;
        
        child = fork();
        switch (child) {
        case -1:
                warn("fork");
                break;
        case 0:
                signal(SIGINT, SIG_DFL);
                print(ckptfile);
                unreachable();
        default:
                break;
        }
        
        waitpid(child, NULL, 0);
}

void interactive(void)
{
        char buf[1024];
        
        signal(SIGINT, interrupt);
        bzero(buf, sizeof(buf));

        for (;;) {
                printf("(ckpt) ");
                fflush(stdout);

                fgets(buf, sizeof(buf), stdin);
                if (feof(stdin) || buf[0] == 'q') {
                        exit(EXIT_SUCCESS);
                }

                if (buf[0] == '\n') {
                        goto next;
                }

                *(char *)strchr(buf, '\n') = '\0';
                
                switch (buf[0]) {
                case 'h':
                        interactive_usage();
                        break;
                case 'c':
                        ckptcmd(buf);
                        break;
                case 'r':
                        restartcmd(buf);
                        break;
                case 'p':
                        printcmd(buf);
                        break;
                default:
                        warnx("Undefined command '%c'", buf[0]);
                        break;
                }
        
        next:
                bzero(buf, sizeof(buf));
        }
}
