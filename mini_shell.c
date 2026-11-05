/*
 * ============================================================
 *  MINI SHELL IN C  (my own small version of bash)
 * ============================================================
 *  What is a shell?
 *    A shell is a program that reads the commands you type and
 *    runs them.
 *
 *  What this shell can do:
 *    1. Run normal commands like ls, cat, grep, echo
 *    2. Built-in commands: cd, history, exit
 *    3. Pipes:            cmd1 | cmd2 | cmd3   (max 3 commands)
 *    4. Redirection:      <   >   >>
 *    5. Joining commands: ;   &&   ||
 *
 *  How a normal command runs (3 steps):
 *    fork   -> make a child process (a copy of the shell)
 *    execvp -> the child turns into the command (like ls)
 *    wait   -> the shell waits until the child finishes
 *
 *  Order of functions in this file (top to bottom):
 *    small helpers -> history -> tokenize -> redirections
 *    -> run normal command -> cd / history / exit
 *    -> pipes -> separators (; && ||) -> main
 * ============================================================
 */

/* Ready-made tools (libraries) that we use */
#include <stdio.h>      /* printf, fgets (print and read text) */
#include <stdlib.h>     /* exit, strtol (stop program, text to number) */
#include <string.h>     /* strcmp, strcpy, strlen (work with text) */
#include <unistd.h>     /* fork, execvp, pipe, dup2, chdir, close, read, write */
#include <sys/types.h>  /* basic types like pid_t (process id) */
#include <sys/wait.h>   /* waitpid, WEXITSTATUS (wait for child process) */
#include <fcntl.h>      /* open, fcntl, O_RDONLY, O_WRONLY ... (open files) */

/* ----------- Fixed numbers (limits) used in the program ----------- */
#define MAX_INPUT_LEN 2048    /* one line can have at most 2048 characters */
#define MAX_ARGS 100          /* one command can have at most 100 words */
#define MAX_HISTORY 2048      /* history keeps at most 2048 commands */
#define MAX_PIPE_STAGES 3     /* a pipe can join at most 3 commands */
#define TOKEN_BUFFER_SIZE 4096 /* space to keep the cut words of a command */

/* The text shown before you type. Change this word to change the prompt.
 * NOTE: for the college assignment, it must be "MTL458 > " */
#define PROMPT "ownshell > "

/* ----------- Global variables (can be used by every function) ----------- */
char previous_dir[MAX_INPUT_LEN + 1] = "";  /* the last folder (used by cd -) */
char shell_home[MAX_INPUT_LEN + 1];         /* the folder where the shell started (used as "home") */
char history[MAX_HISTORY][MAX_INPUT_LEN + 1]; /* list of all typed commands */
int history_count = 0;                      /* how many commands are saved in history */


/* ============================================================
 *  SMALL HELPER FUNCTIONS
 * ============================================================ */

/*
 * send_error:
 *   A child process uses this to tell its parent "I failed".
 *   It writes one small byte into a special tube (pipe).
 *   The parent reads this byte and knows the command failed.
 */
void send_error(int fd) {
    char error = 1;                          /* the secret signal */
    ssize_t written = write(fd, &error, 1);  /* send 1 byte into the tube */
    (void)written;                           /* we do not use this value */
}

/*
 * report_invalid:
 *   Prints "Invalid Command" in the place given by fd.
 *   We use write() on the real screen, so the message still
 *   shows on the screen even if the output was sent to a file.
 */
void report_invalid(int fd) {
    const char *message = "Invalid Command\n";
    ssize_t written = write(fd, message, strlen(message));
    (void)written;
}


/* ============================================================
 *  HISTORY FUNCTIONS
 * ============================================================ */

/*
 * add_history:
 *   Saves one typed line in the history list.
 *   If the list is full (2048), it removes the oldest line,
 *   moves all the others one place up, and adds the new line at the end.
 */
void add_history(const char *line) {
    if (history_count < MAX_HISTORY) {
        /* There is space: save the line at the next free place */
        strcpy(history[history_count], line);
        history_count++;
    }
    else {
        /* List is full: shift every line one place up (oldest is removed) */
        for (int i = 1; i < MAX_HISTORY; i++) {
            strcpy(history[i - 1], history[i]);
        }
        /* Put the new line at the end */
        strcpy(history[MAX_HISTORY - 1], line);
    }
}

/*
 * print_history:
 *   Prints the last n commands.
 *   If n is negative or bigger than what we have, print everything.
 */
void print_history(int n) {
    int start;
    if (n < 0 || n > history_count) {
        n = history_count;            /* show all */
    }
    start = history_count - n;        /* where to start printing */
    for (int i = start; i < history_count; i++) {
        printf("%s\n", history[i]);
    }
}


/* ============================================================
 *  TOKENIZE: cut a command into words
 * ============================================================ */

/*
 * tokenize:
 *   Cuts one command line into separate words and puts them in argv.
 *   Example: "echo   hello  world" -> argv = {"echo", "hello", "world"}
 *   - Extra spaces are ignored.
 *   - <  >  >> are made separate words, even if there is no space
 *     around them. Example: "sort<a.txt" -> {"sort", "<", "a.txt"}
 *   - The words are stored in 'buffer' (a box of letters).
 *   - It gives back argc = how many words we got.
 */
int tokenize(char *line, char *argv[], char *buffer) {
    int i = 0;      /* position in the line */
    int argc = 0;   /* number of words found */
    int pos = 0;    /* position in the buffer */
    while (line[i] != '\0') {
        /* Skip spaces, tabs and new lines */
        while (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') {
            i++;
        }
        /* If the line ended after the spaces, stop */
        if (line[i] == '\0') {
            break;
        }
        /* Input redirection sign: < (make it its own word) */
        if (line[i] == '<') {
            if (argc >= MAX_ARGS) {
                break;
            }
            argv[argc++] = &buffer[pos];
            buffer[pos++] = '<';
            buffer[pos++] = '\0';
            i++;
            continue;
        }
        /* Append sign: >> (two letters, make it one word) */
        if (line[i] == '>' && line[i + 1] == '>') {
            if (argc >= MAX_ARGS) {
                break;
            }
            argv[argc++] = &buffer[pos];
            buffer[pos++] = '>';
            buffer[pos++] = '>';
            buffer[pos++] = '\0';
            i += 2;
            continue;
        }
        /* Output redirection sign: > (make it its own word) */
        if (line[i] == '>') {
            if (argc >= MAX_ARGS) {
                break;
            }
            argv[argc++] = &buffer[pos];
            buffer[pos++] = '>';
            buffer[pos++] = '\0';
            i++;
            continue;
        }
        /* A normal word: copy letters until a space or < or > comes */
        if (argc >= MAX_ARGS) {
            break;
        }
        argv[argc++] = &buffer[pos];
        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' && line[i] != '\n' && line[i] != '<' && line[i] != '>') {
            buffer[pos++] = line[i];
            i++;
        }
        buffer[pos++] = '\0';   /* end mark of this word */
    }
    argv[argc] = NULL;   /* the list ends with NULL */

    return argc;
}


/* ============================================================
 *  PARSE_REDIRECTIONS: find <  >  >> and the file names
 * ============================================================ */

/*
 * parse_redirections:
 *   Looks through the words and finds <  >  >> with their file names.
 *   It removes them from argv, so only the real command stays.
 *   Example: {"sort", "<", "data.txt", ">", "out.txt"}
 *     -> argv becomes {"sort"}
 *     -> infile  = "data.txt"
 *     -> outfile = "out.txt"
 *     -> append  = 0   (it is 1 for >>)
 *   It gives back the new number of words,
 *   or -1 if something is wrong (two < signs, missing file name ...).
 */
int parse_redirections(char *argv[], int argc, char **infile, char **outfile, int *append) {
    char *new_argv[MAX_ARGS + 1];   /* the clean list without < > >> */
    int new_argc = 0;
    *infile = NULL;
    *outfile = NULL;
    *append = 0;
    for (int i = 0; i < argc; i++) {
        /* Case 1: "< file" (take input from a file) */
        if (strcmp(argv[i], "<") == 0) {
            /* Error: already have an input file, or no file name after < */
            if (*infile != NULL || i + 1 >= argc) {
                return -1;
            }
            /* Error: the next word is another sign, not a file name */
            if (strcmp(argv[i + 1], "<") == 0 || strcmp(argv[i + 1], ">") == 0 || strcmp(argv[i + 1], ">>") == 0) {
                return -1;
            }
            *infile = argv[++i];   /* remember the file name, skip it */
        }
        /* Case 2: "> file" or ">> file" (send output to a file) */
        else if (strcmp(argv[i], ">") == 0 ||  strcmp(argv[i], ">>") == 0) {
            /* Error: already have an output file, or no file name */
            if (*outfile != NULL || i + 1 >= argc) {
                return -1;
            }
            /* Error: the next word is another sign, not a file name */
            if (strcmp(argv[i + 1], "<") == 0 || strcmp(argv[i + 1], ">") == 0 || strcmp(argv[i + 1], ">>") == 0) {
                return -1;
            }
            /* >> means add at the end, > means erase old content */
            if (strcmp(argv[i], ">>") == 0) {
                *append = 1;
            }
            else {
                *append = 0;
            }
            *outfile = argv[++i];  /* remember the file name, skip it */
        }
        /* Case 3: a normal word, keep it in the clean list */
        else {
            if (new_argc >= MAX_ARGS) {
                return -1;
            }
            new_argv[new_argc++] = argv[i];
        }
    }
    new_argv[new_argc] = NULL;
    /* Copy the clean list back into argv */
    for (int i = 0; i <= new_argc; i++) {
        argv[i] = new_argv[i];
    }
    return new_argc;
}


/* ============================================================
 *  EXECUTE_COMMAND: run a normal command (ls, cat, grep ...)
 * ============================================================ */

/*
 * execute_command:
 *   Runs one normal command (not cd / history / exit, no pipe).
 *   Steps:
 *     1. Find < > >> in the words (parse_redirections)
 *     2. Make a small tube (error_pipe) so the child can tell us if it failed
 *     3. fork()   -> make a child process
 *     4. In the child:
 *          - connect input/output files if needed
 *          - execvp() -> turn the child into the command
 *     5. In the parent: waitpid() -> wait for the child to finish
 *   It gives back the exit status: 0 = worked, not 0 = failed.
 *   Example: "ls > list.txt" saves the file list into list.txt
 */
int execute_command(char *argv[]) {
    char *infile = NULL;     /* file for input (<) */
    char *outfile = NULL;    /* file for output (> or >>) */
    int append = 0;          /* 1 if >> was used */
    int argc = 0;            /* number of words */
    int error_pipe[2];       /* tube for the "I failed" signal: [0]=read end, [1]=write end */
    pid_t pid;               /* process id of the child */
    int status;              /* how the child ended */

    /* Count the words (the list ends with NULL) */
    while (argv[argc] != NULL) {
        argc++;
    }
    /* Find and remove < > >> from the words */
    argc = parse_redirections(argv, argc, &infile, &outfile, &append);
    if (argc <= 0) {
        printf("Invalid Command\n");
        return 1;
    }
    /* Make the error tube */
    if (pipe(error_pipe) < 0) {
        printf("Invalid Command\n");
        return 1;
    }
    /* Make the child process */
    pid = fork();
    if (pid < 0) {
        /* fork failed */
        close(error_pipe[0]);
        close(error_pipe[1]);
        printf("Invalid Command\n");
        return 1;
    }

    /* -------- This part runs only in the CHILD (pid == 0) -------- */
    if (pid == 0) {
        close(error_pipe[0]);   /* the child does not read from the error tube */

        /* Keep a copy of the real screen. If something fails later,
         * we can still print "Invalid Command" on the screen,
         * even when the normal output goes to a file. */
        int output_fd = dup(STDOUT_FILENO);
        if (output_fd < 0) {
            send_error(error_pipe[1]);
            close(error_pipe[1]);
            _exit(1);
        }
        /* This copy must close by itself when the command starts running */
        if (fcntl(output_fd, F_SETFD, FD_CLOEXEC) < 0) {
            close(output_fd);
            send_error(error_pipe[1]);
            close(error_pipe[1]);
            _exit(1);
        }

        /* Input redirection (<): read input from a file, not the keyboard */
        if (infile != NULL) {
            int fd = open(infile, O_RDONLY);   /* open the file to read */
            if (fd < 0) {
                /* file not found */
                report_invalid(output_fd);
                send_error(error_pipe[1]);
                close(output_fd);
                close(error_pipe[1]);
                _exit(1);
            }
            /* Make the file become the input of the command */
            if (dup2(fd, STDIN_FILENO) < 0) {
                close(fd);
                report_invalid(output_fd);
                send_error(error_pipe[1]);
                close(output_fd);
                close(error_pipe[1]);
                _exit(1);
            }
            close(fd);
        }

        /* Output redirection (> or >>): write output to a file, not the screen */
        if (outfile != NULL) {
            int flags = O_WRONLY | O_CREAT;    /* write only, make the file if missing */
            if (append) {
                flags |= O_APPEND;             /* >> : add at the end */
            }
            else {
                flags |= O_TRUNC;              /* >  : erase old content first */
            }
            int fd = open(outfile, flags, 0644);
            if (fd < 0) {
                report_invalid(output_fd);
                send_error(error_pipe[1]);
                close(output_fd);
                close(error_pipe[1]);
                _exit(1);
            }
            /* Make the file become the output of the command */
            if (dup2(fd, STDOUT_FILENO) < 0) {
                close(fd);
                report_invalid(output_fd);
                send_error(error_pipe[1]);
                close(output_fd);
                close(error_pipe[1]);
                _exit(1);
            }
            close(fd);
        }

        /*
         * Now turn the child into the command (like ls).
         * If it works, the code below never runs.
         * We must not print the error here with printf, because the
         * output may already be going to a file.
         */
        execvp(argv[0], argv);

        /* If we reach here, execvp failed (command not found) */
        report_invalid(output_fd);
        send_error(error_pipe[1]);
        close(output_fd);
        close(error_pipe[1]);
        _exit(127);
    }

    /* -------- This part runs only in the PARENT (the shell) -------- */
    close(error_pipe[1]);   /* the parent does not write to the error tube */

    /* Wait until the child finishes */
    if (waitpid(pid, &status, 0) < 0) {
        close(error_pipe[0]);
        printf("Invalid Command\n");
        return 1;
    }
    /* Check if the child sent the "I failed" byte */
    char error;
    ssize_t bytes = read(error_pipe[0], &error, 1);
    close(error_pipe[0]);
    if (bytes > 0) {
        return 1;   /* the child said it failed */
    }
    /* Give back the exit status of the child (0 = worked) */
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}


/* ============================================================
 *  BUILT-IN COMMANDS: cd, history, exit
 * ============================================================ */

/*
 * handle_cd:
 *   Does the "cd" command (change folder).
 *   cd or cd ~  -> go to the folder where the shell started
 *   cd -        -> go back to the last folder and print its address
 *   cd path     -> go to that folder (also works for  .  and  .. )
 *   Wrong folder or too many words -> print "Invalid Command"
 *   Before moving, it saves the old folder in previous_dir.
 *   error_fd is the place where "Invalid Command" is written.
 */
int handle_cd(char *argv[], int argc, int error_fd) {
    char current_dir[MAX_INPUT_LEN + 1];   /* the folder we are in now */

    /* Case 1: "cd" or "cd ~"  -> go home */
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "~") == 0)) {
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {   /* get the current folder */
            report_invalid(error_fd);
            return 1;
        }
        if (chdir(shell_home) != 0) {   /* move to the start folder */
            report_invalid(error_fd);
            return 1;
        }
        strcpy(previous_dir, current_dir);   /* remember where we came from */

        return 0;
    }

    /* Case 2: "cd -"  -> go back to the previous folder */
    if (argc == 2 && strcmp(argv[1], "-") == 0) {
        if (previous_dir[0] == '\0') {   /* there is no previous folder yet */
            report_invalid(error_fd);
            return 1;
        }
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            report_invalid(error_fd);
            return 1;
        }
        if (chdir(previous_dir) != 0) {  /* go back */
            report_invalid(error_fd);
            return 1;
        }
        strcpy(previous_dir, current_dir);   /* now the old "current" is the "previous" */
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            report_invalid(error_fd);
            return 1;
        }
        printf("%s\n", current_dir);   /* cd - prints the new address */

        return 0;
    }

    /* Case 3: "cd path"  -> go to that folder */
    if (argc == 2) {
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            report_invalid(error_fd);
            return 1;
        }
        if (chdir(argv[1]) != 0) {   /* folder not found -> error */
            report_invalid(error_fd);
            return 1;
        }
        strcpy(previous_dir, current_dir);
        return 0;
    }

    /* Anything else (like cd a b) is wrong */
    report_invalid(error_fd);
    return 1;
}

/*
 * handle_history:
 *   Does the "history" command.
 *   history     -> show all saved commands
 *   history n   -> show only the last n commands
 *   Wrong input (like history abc or history -1) -> "Invalid Command"
 */
int handle_history(char *argv[], int argc, int error_fd) {
    char *end;     /* tells where the number ended (used to check the text) */
    long value;    /* the number n */
    if (argc == 1) {
        print_history(-1);   /* -1 means show all */
        return 0;
    }
    if (argc == 2) {
        value = strtol(argv[1], &end, 10);   /* change the text "3" into the number 3 */
        /* Wrong if: empty text, extra letters after the number, or negative */
        if (*argv[1] == '\0' || *end != '\0' || value < 0) {
            report_invalid(error_fd);
            return 1;
        }
        print_history((int)value);
        return 0;
    }
    report_invalid(error_fd);
    return 1;
}

/*
 * execute_builtin_with_redirection:
 *   Runs cd, history or exit. It also supports < and > for them.
 *   Example: "history > h.txt" saves the history in h.txt
 *   Steps:
 *     1. Find < > >> (parse_redirections)
 *     2. Save a copy of the real input and output
 *     3. Connect the files if needed
 *     4. Run cd / history / exit
 *     5. Put the real input and output back
 */
int execute_builtin_with_redirection(char *argv[], int argc) {
    char *infile = NULL;
    char *outfile = NULL;
    int append = 0;
    int redir_argc;
    int saved_stdin;    /* copy of the real keyboard input */
    int saved_stdout;   /* copy of the real screen output */
    int status;
    redir_argc = parse_redirections(argv, argc, &infile, &outfile, &append);
    if (redir_argc <= 0) {
        printf("Invalid Command\n");
        return 1;
    }
    /* Save copies of the real input and output, to restore them later */
    saved_stdin = dup(STDIN_FILENO);
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdin < 0 || saved_stdout < 0) {
        if (saved_stdin >= 0) close(saved_stdin);
        if (saved_stdout >= 0) close(saved_stdout);
        printf("Invalid Command\n");
        return 1;
    }
    /* Input from a file (<) */
    if (infile != NULL) {
        int fd = open(infile, O_RDONLY);
        if (fd < 0) {
            report_invalid(saved_stdout);
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }
        if (dup2(fd, STDIN_FILENO) < 0) {
            close(fd);
            report_invalid(saved_stdout);
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }
        close(fd);
    }
    /* Output to a file (> or >>) */
    if (outfile != NULL) {
        int flags = O_WRONLY | O_CREAT;
        if (append) flags |= O_APPEND;
        else flags |= O_TRUNC;
        int fd = open(outfile, flags, 0644);
        if (fd < 0) {
            report_invalid(saved_stdout);
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }
        if (dup2(fd, STDOUT_FILENO) < 0) {
            close(fd);
            report_invalid(saved_stdout);
            dup2(saved_stdin, STDIN_FILENO);
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdin);
            close(saved_stdout);
            return 1;
        }
        close(fd);
    }
    /* Run the right built-in command */
    if (strcmp(argv[0], "history") == 0) {
        status = handle_history(argv, redir_argc, saved_stdout);
    }
    else if (strcmp(argv[0], "cd") == 0) {
        status = handle_cd(argv, redir_argc, saved_stdout);
    }
    else {
        /* It must be "exit": put things back and stop the shell */
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdin);
        close(saved_stdout);
        exit(0);
    }

    /* Put the real input and output back */
    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    return status;
}


/* ============================================================
 *  PIPES:  cmd1 | cmd2 | cmd3
 * ============================================================ */

/*
 * split_pipeline:
 *   Cuts a line at every | sign, so one line becomes separate commands.
 *   Example: "cat a.txt | grep x | wc -l"
 *     -> commands[0] = "cat a.txt "
 *     -> commands[1] = " grep x "
 *     -> commands[2] = " wc -l"
 *   It gives back how many commands there are.
 *   More than 3 commands gives -1 (error).
 */
int split_pipeline(char *line, char *commands[]) {
    int count = 0;
    char *start = line;
    while (1) {
        if (count >= MAX_PIPE_STAGES) {
            return -1;   /* too many commands */
        }
        commands[count++] = start;
        char *pipe_pos = strchr(start, '|');   /* find the next | */
        if (pipe_pos == NULL) {
            break;   /* no more | signs */
        }
        *pipe_pos = '\0';        /* cut the text here */
        start = pipe_pos + 1;    /* the next command starts after | */
    }
    return count;
}

/*
 * execute_pipeline:
 *   Runs commands joined by pipes.
 *   Example: "cat data.txt | grep gamma"
 *   Steps:
 *     1. Make the pipes (tubes) between the commands
 *     2. Make one child for each command
 *     3. In each child: connect its input/output to the tubes,
 *        close all tube ends it does not need, then run the command
 *     4. The parent closes its tube ends and waits for every child
 *   The exit status of the LAST command is the result of the pipeline.
 *   If one command is not found, "Invalid Command" is printed for it,
 *   and the other commands still run.
 */
int execute_pipeline(char *commands[], int count) {
    int pipes[MAX_PIPE_STAGES - 1][2];     /* tubes between commands */
    int error_pipes[MAX_PIPE_STAGES][2];   /* one "I failed" tube for each command */
    pid_t pids[MAX_PIPE_STAGES];           /* process id of each child */

    /* Make the normal tubes (one less than the number of commands) */
    for (int i = 0; i < count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            printf("Invalid Command\n");
            return 1;
        }
    }
    /* Make the error tubes (one for each command) */
    for (int i = 0; i < count; i++) {
        if (pipe(error_pipes[i]) < 0) {
            printf("Invalid Command\n");
            return 1;
        }
    }

    /* Make the children, one for each command */
    for (int i = 0; i < count; i++) {
        char *argv[MAX_ARGS + 1];
        char buffer[TOKEN_BUFFER_SIZE];
        int argc = tokenize(commands[i], argv, buffer);   /* cut into words */
        if (argc == 0) {
            pids[i] = -1;   /* empty command, nothing to run */
            continue;
        }
        char *infile = NULL;
        char *outfile = NULL;
        int append = 0;
        argc = parse_redirections(argv, argc, &infile, &outfile, &append);
        if (argc <= 0) {
            pids[i] = -1;
            continue;
        }
        pids[i] = fork();
        if (pids[i] < 0) {
            printf("Invalid Command\n");
            pids[i] = -1;
            continue;
        }

        /* -------- CHILD number i -------- */
        if (pids[i] == 0) {
            close(error_pipes[i][0]);
            /* Keep a copy of the real screen for error messages */
            int output_fd = dup(STDOUT_FILENO);
            if (output_fd < 0) {
                send_error(error_pipes[i][1]);
                close(error_pipes[i][1]);
                _exit(1);
            }
            if (fcntl(output_fd, F_SETFD, FD_CLOEXEC) < 0) {
                close(output_fd);
                send_error(error_pipes[i][1]);
                close(error_pipes[i][1]);
                _exit(1);
            }
            /* If this is not the first command, read input from the previous tube */
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
            }
            /* If this is not the last command, write output into the next tube */
            if (i < count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
            }
            /* Input from a file (<) */
            if (infile != NULL) {
                int fd = open(infile, O_RDONLY);
                if (fd < 0) {
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
                if (dup2(fd, STDIN_FILENO) < 0) {
                    close(fd);
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
                close(fd);
            }
            /* Output to a file (> or >>) */
            if (outfile != NULL) {
                int flags = O_WRONLY | O_CREAT;
                if (append) {
                    flags |= O_APPEND;
                }
                else {
                    flags |= O_TRUNC;
                }
                int fd = open(outfile, flags, 0644);
                if (fd < 0) {
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
                if (dup2(fd, STDOUT_FILENO) < 0) {
                    close(fd);
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
                close(fd);
            }
            /*
             * IMPORTANT: close ALL tube ends in the child.
             * The ones we need are already connected by dup2 above.
             * If we forget to close the extra ends, the shell can hang forever.
             */
            for (int j = 0; j < count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            /* cd, history and exit are not allowed inside a pipe */
            if (strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "history") == 0 || strcmp(argv[0], "exit") == 0) {
                report_invalid(output_fd);
                send_error(error_pipes[i][1]);
                close(output_fd);
                close(error_pipes[i][1]);
                _exit(1);
            }
            /* Turn this child into the command */
            execvp(argv[0], argv);
            /* If we reach here, the command was not found */
            report_invalid(output_fd);
            send_error(error_pipes[i][1]);
            close(output_fd);
            close(error_pipes[i][1]);
            _exit(127);
        }
    }

    /* -------- PARENT (the shell) -------- */

    /* The parent closes all normal tube ends (it does not use them) */
    for (int i = 0; i < count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    /* The parent closes the write end of every error tube */
    for (int i = 0; i < count; i++) {
        close(error_pipes[i][1]);
    }
    /* Wait for every command to finish */
    int last_status = 1;
    for (int i = 0; i < count; i++) {
        if (pids[i] <= 0) {
            /* this command never started */
            close(error_pipes[i][0]);
            continue;
        }
        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            close(error_pipes[i][0]);
            continue;
        }
        /* Check if this child sent the "I failed" byte */
        char error;
        ssize_t bytes = read(error_pipes[i][0], &error, 1);
        close(error_pipes[i][0]);
        if (bytes > 0) {
            last_status = 1;
        }
        /* The last command decides the final result */
        if (i == count - 1) {
            if (WIFEXITED(status)) {
                last_status = WEXITSTATUS(status);
            }
            else {
                last_status = 1;
            }
        }
    }
    return last_status;
}


/* ============================================================
 *  RUN ONE SMALL COMMAND
 * ============================================================ */

/*
 * run_segment:
 *   Runs one small command (the part between ; && ||).
 *   It chooses how to run it:
 *     has a |                      -> pipeline
 *     cd / history / exit          -> built-in
 *     anything else (ls, cat ...)  -> normal command
 *   It gives back the exit status (0 = worked).
 */
int run_segment(char *segment){
    /* Is there a pipe in this command? */
    if (strchr(segment, '|') != NULL) {
        char *commands[MAX_PIPE_STAGES];
        int count = split_pipeline(segment, commands);
        if (count < 1 || count > MAX_PIPE_STAGES) {
            printf("Invalid Command\n");
            return 1;
        }
        return execute_pipeline(commands, count);
    }
    /* No pipe: cut the command into words */
    char *argv[MAX_ARGS + 1];
    char buffer[TOKEN_BUFFER_SIZE];
    int argc = tokenize(segment, argv, buffer);
    if (argc == 0) {
        return 1;
    }
    /* Built-in commands */
    if (strcmp(argv[0], "cd") == 0 ||
        strcmp(argv[0], "history") == 0 ||
        strcmp(argv[0], "exit") == 0) {
        return execute_builtin_with_redirection(argv, argc);
    }
    /* External command (a ready-made program like ls) */
    return execute_command(argv);
}


/* ============================================================
 *  SEPARATORS:  ;   &&   ||
 * ============================================================ */

/*
 * find_separator:
 *   Finds the first  ;  &&  or  ||  in the line.
 *   It gives back a type number:
 *     0 = none found,  1 = ;,  2 = &&,  3 = ||
 *   It also tells where the sign is (separator_pos) and how long it is.
 *   A single | is NOT a separator, because it is a pipe.
 */
int find_separator(char *line, int *separator_length, char **separator_pos) {
    int i = 0;
    while (line[i] != '\0') {
        /* ; */
        if (line[i] == ';') {
            *separator_length = 1;
            *separator_pos = &line[i];
            return 1;
        }
        /* && */
        if (line[i] == '&' && line[i + 1] == '&') {
            *separator_length = 2;
            *separator_pos = &line[i];
            return 2;
        }
        /* || (two bars together are a separator, not a pipe) */
        if (line[i] == '|' && line[i + 1] == '|') {
            *separator_length = 2;
            *separator_pos = &line[i];
            return 3;
        }
        /* A single | is a pipe, so we skip it here */
        i++;
    }
    *separator_length = 0;
    *separator_pos = NULL;
    return 0;
}

/*
 * execute_line:
 *   Runs a full typed line from left to right.
 *   It cuts the line at ; && || and runs each small command.
 *   It decides if the next command should run:
 *     ;   -> always run the next
 *     &&  -> run the next only if the last one worked (status 0)
 *     ||  -> run the next only if the last one failed (status not 0)
 *   Example: "madeupcmd || echo recovered"
 *     -> madeupcmd fails -> || runs the next -> prints "recovered"
 */
int execute_line(char *line) {
    char *current = line;      /* where we are in the line */
    int last_status = 0;       /* result of the last command (0 = worked) */
    int should_run = 1;        /* should the next command run? 1 = yes */
    while (*current != '\0') {
        int separator_length;
        char *separator_pos;
        int separator_type = find_separator(current, &separator_length, &separator_pos);
        char segment[MAX_INPUT_LEN + 1];   /* the small command we will run */

        /* Copy the command that comes before the separator */
        if (separator_pos != NULL) {
            int length = (int)(separator_pos - current);
            if (length > MAX_INPUT_LEN) {
                printf("Invalid Command\n");
                return 1;
            }
            memcpy(segment, current, length);
            segment[length] = '\0';
        }
        else {
            /* No more separators: take the rest of the line */
            strncpy(segment, current, MAX_INPUT_LEN);
            segment[MAX_INPUT_LEN] = '\0';
        }

        /* Remove the spaces at the start */
        char *start = segment;
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        /* Remove the spaces at the end */
        char *end = start + strlen(start);
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        *end = '\0';

        /* Run this command only if it is not empty and it is allowed to run */
        if (*start != '\0' && should_run) {
            last_status = run_segment(start);
        }

        /* No more separators: the line is finished */
        if (separator_pos == NULL) {
            break;
        }
        /* Move to the text after the separator */
        current = separator_pos + separator_length;

        /* Decide if the next command should run */
        if (separator_type == 1) {
            should_run = 1;                    /* ;  : always */
        }
        else if (separator_type == 2) {
            should_run = (last_status == 0);   /* && : only if the last worked */
        }
        else {
            should_run = (last_status != 0);   /* || : only if the last failed */
        }
    }
    return last_status;
}


/* ============================================================
 *  MAIN: where the program starts
 * ============================================================ */

/*
 * main:
 *   The boss of the program. It does this again and again:
 *     1. Show the prompt
 *     2. Read a line you typed
 *     3. Ignore it if it is empty
 *     4. Save it in history (exactly as typed)
 *     5. Run it with execute_line
 *   It stops when you type "exit" (or when the input ends).
 */
int main(void) {
    char input[MAX_INPUT_LEN + 2];          /* the line you type */
    char history_line[MAX_INPUT_LEN + 1];   /* a safe copy of the line for history */

    /* Remember the folder where the shell started (it is the "home" for cd) */
    if (getcwd(shell_home, sizeof(shell_home)) == NULL) {
        printf("Invalid Command\n");
        return 1;
    }
    while (1) {
        /* Show the prompt */
        printf("%s", PROMPT);
        fflush(stdout);   /* show it on the screen right now */

        /* Read the line. If there is no more input, stop. */
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        /* Remove the new line character at the end */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }

        /* If the line has only spaces (or nothing), ignore it */
        int only_whitespace = 1;
        for (int i = 0; input[i] != '\0'; i++) {
            if (input[i] != ' ' && input[i] != '\t') {
                only_whitespace = 0;
                break;
            }
        }
        if (only_whitespace) {
            continue;   /* go back and show the prompt again */
        }

        /* Save the original line in history before changing it */
        strcpy(history_line, input);
        add_history(history_line);

        /* Run the line */
        execute_line(input);
    }
    return 0;
}
