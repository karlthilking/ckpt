/* ckptrepl.c */
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <stdio.h>
#include "launch.h"

static const char *helpmsg =
"OVERVIEW: MacOS Checkpoint-Restart REPL\n\n"
"USAGE: <sub-command> <args> ...\n\n"
"OPTIONS:\n"
"  p, print <ckpt-file>                 Print checkpoint file\n"
"  c, checkpoint <binary> <args>        Execute binary with libckpt\n"
"  r, restart <ckpt-file>               Restart from checkpoint file\n"
"  s, shell <shell-command>             Issue a shell command\n"
"  h, help                              Display this help message\n"
"  q, quit                              Exit this program\n";

static const char *ckpt_helpmsg = 
"     OVERVIEW: Checkpoint Command\n"
"        USAGE: c, checkpoint <binary> <args> ...\n"
"  DESCRIPTION: Execute a binary injected with libckpt.dylib\n";

static const char *restart_helpmsg =
"     OVERVIEW: Restart Command\n"
"        USAGE: r, restart <ckpt-file>\n"
"  DESCRIPTION: Restart from a saved checkpoint file\n";

static const char *print_helpmsg =
"     OVERVIEW: Print Checkpoint Command\n"
"        USAGE: p, print <ckpt-file>\n"
"  DESCRIPTION: Print a saved checkpoint file\n";

static const char *shell_helpmsg =
"     OVERVIEW: Shell Commands\n"
"        USAGE: s, shell <shell-command> ...\n"
"  DESCRIPTION: Issue a shell command when running this program\n";

enum {
        UNDEFINED       = -1,
        CHECKPOINT      = 0,
        RESTART         = 1,
        PRINT           = 2,
        SHELL           = 3,
        HELP            = 4,
        QUIT            = 5
};

static pid_t child = -1;

int cmdlookup(int c)
{
        static s8 table[256];
        static bool init = false;

        if (!init) {
                memset(table, UNDEFINED, sizeof(table));
                table['c'] = CHECKPOINT;
                table['r'] = RESTART;
                table['p'] = PRINT;
                table['s'] = SHELL;
                table['h'] = HELP;
                table['q'] = QUIT;
                init = true;
        }

        return table[c];
}

int cmdtype(char *cmd)
{
        if (cmd[1] == '\0' || isspace(cmd[1])) {
                return cmdlookup(cmd[0]);
        }

        switch (cmd[0]) {
        case 'c':
                if (strncmp(cmd, "checkpoint", 10) == 0) {
                        return CHECKPOINT;
                }
                return UNDEFINED;
        case 'r':
                if (strncmp(cmd, "restart", 7) == 0) {
                        return RESTART;
                }
                return UNDEFINED;
        case 'p':
                if (strncmp(cmd, "print", 5) == 0) {
                        return PRINT;
                }
                return UNDEFINED;
        case 's':
                if (strncmp(cmd, "shell", 5) == 0) {
                        return SHELL;
                }
                return UNDEFINED;
        case 'h':
                if (strncmp(cmd, "help", 4) == 0) {
                        return HELP;
                }
                return UNDEFINED;
        case 'q':
                if (strncmp(cmd, "quit", 4) == 0) {
                        return QUIT;
                }
                return UNDEFINED;
        default:
                return UNDEFINED;
        }
}

void replusage(void)
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

void shellcmd_usage(void)
{
        dprintf(STDERR_FILENO, "\n%s\n", shell_helpmsg);
}

void replerror(char *cmd)
{
        dprintf(STDERR_FILENO, "Undefined command: '%s'\n", cmd);
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
        
        argv = malloc(sizeof(char *) * (1 << 5));
        getargv(cmd, argv);

        if (argv[0] == NULL) {
                ckptcmd_usage();
                return;
        }
        
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
        
        assert(strtok(cmd, " "));
        if ((ckptfile = strtok(NULL, " ")) == NULL) {
                restartcmd_usage();
                return;
        }
        
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
        
        assert(strtok(cmd, " "));
        if ((ckptfile = strtok(NULL, " ")) == NULL) {
                printcmd_usage();
                return;
        }
        
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

void shellcmd(char *cmd)
{
        char *str;

        if (strncmp(cmd, "s ", 2) == 0) {
                str = cmd + 2;
        } else if (strncmp(cmd, "shell", 5) == 0) {
                str = cmd + 5;
        }

        system(str);
}

void interactive(void)
{
        char *str, buf[1024];
        
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
                } else if (strncmp(buf, "clear", 5) == 0) {
                        system("clear");
                        goto next;
                }
                
                str = buf + strspn(buf, " \t\n\r\f\v");
                *(char *)strchr(str, '\n') = '\0';
                
                switch (cmdtype(str)) {
                case HELP:
                        replusage();
                        break;
                case CHECKPOINT:
                        ckptcmd(str);
                        break;
                case RESTART:
                        restartcmd(str);
                        break;
                case PRINT:
                        printcmd(str);
                        break;
                case SHELL:
                        shellcmd(str);
                        break;
                case UNDEFINED:
                        replerror(str);
                        break;
                }
        
        next:
                bzero(buf, sizeof(buf));
        }
}
