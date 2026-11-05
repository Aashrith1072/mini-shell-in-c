#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_INPUT_LEN 2048
#define MAX_ARGS 100
#define MAX_HISTORY 2048
#define MAX_PIPE_STAGES 3
#define TOKEN_BUFFER_SIZE 4096

#define PROMPT "ownshell > "

char previous_dir[MAX_INPUT_LEN + 1] = "";
char shell_home[MAX_INPUT_LEN + 1];
char history[MAX_HISTORY][MAX_INPUT_LEN + 1];
int history_count = 0;

void send_error(int fd) {
    char error = 1;
    ssize_t written = write(fd, &error, 1);
    (void)written;
}
void report_invalid(int fd) {
    const char *message = "Invalid Command\n";
    ssize_t written = write(fd, message, strlen(message));
    (void)written;
}
void add_history(const char *line) {
    if (history_count < MAX_HISTORY) {
        strcpy(history[history_count], line);
        history_count++;
    }
    else {
        for (int i = 1; i < MAX_HISTORY; i++) {
            strcpy(history[i - 1], history[i]); 
        }
        strcpy(history[MAX_HISTORY - 1], line);
    }
}
void print_history(int n) {
    int start;
    if (n < 0 || n > history_count) {
        n = history_count;
    }
    start = history_count - n;
    for (int i = start; i < history_count; i++) {
        printf("%s\n", history[i]);
    }
}
int tokenize(char *line, char *argv[], char *buffer) {
    int i = 0;
    int argc = 0;
    int pos = 0;
    while (line[i] != '\0') {
        /* Skip whitespace. */
        while (line[i] == ' ' || line[i] == '\t' || line[i] == '\n') {
            i++;
        }
        if (line[i] == '\0') {
            break;
        }
        /* Input redirection. */
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
        /* Append redirection. */
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
        /* Output redirection. */
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
        /* Normal argument. */
        if (argc >= MAX_ARGS) {
            break;
        }
        argv[argc++] = &buffer[pos];
        while (line[i] != '\0' && line[i] != ' ' && line[i] != '\t' && line[i] != '\n' && line[i] != '<' && line[i] != '>') {
            buffer[pos++] = line[i];
            i++;
        }
        buffer[pos++] = '\0';
    }
    argv[argc] = NULL;

    return argc;
}
int parse_redirections(char *argv[], int argc, char **infile, char **outfile, int *append) {
    char *new_argv[MAX_ARGS + 1];
    int new_argc = 0;
    *infile = NULL;
    *outfile = NULL;
    *append = 0;
    for (int i = 0; i < argc; i++) {
        /* < file */
        if (strcmp(argv[i], "<") == 0) {
            if (*infile != NULL || i + 1 >= argc) {
                return -1;
            }
            if (strcmp(argv[i + 1], "<") == 0 || strcmp(argv[i + 1], ">") == 0 || strcmp(argv[i + 1], ">>") == 0) {
                return -1;
            }
            *infile = argv[++i];
        }
        /* > file or >> file */
        else if (strcmp(argv[i], ">") == 0 ||  strcmp(argv[i], ">>") == 0) {
            if (*outfile != NULL || i + 1 >= argc) {
                return -1;
            }
            if (strcmp(argv[i + 1], "<") == 0 || strcmp(argv[i + 1], ">") == 0 || strcmp(argv[i + 1], ">>") == 0) {
                return -1;
            }
            if (strcmp(argv[i], ">>") == 0) {
                *append = 1;
            }
            else {
                *append = 0;
            }
            *outfile = argv[++i];
        }
        /* Normal argument. */
        else {
            if (new_argc >= MAX_ARGS) {
                return -1;
            }
            new_argv[new_argc++] = argv[i];
        }
    }
    new_argv[new_argc] = NULL;
    for (int i = 0; i <= new_argc; i++) {
        argv[i] = new_argv[i];
    }
    return new_argc;
}
int execute_command(char *argv[]) {
    char *infile = NULL;
    char *outfile = NULL;
    int append = 0;
    int argc = 0;
    int error_pipe[2];
    pid_t pid;
    int status;
    while (argv[argc] != NULL) {
        argc++;
    }
    argc = parse_redirections(argv, argc, &infile, &outfile, &append);
    if (argc <= 0) {
        printf("Invalid Command\n");
        return 1;
    }
    if (pipe(error_pipe) < 0) {
        printf("Invalid Command\n");
        return 1;
    }
    pid = fork();
    if (pid < 0) {
        close(error_pipe[0]);
        close(error_pipe[1]);
        printf("Invalid Command\n");
        return 1;
    }
    if (pid == 0) {
        close(error_pipe[0]);
        /* Input redirection. */
        int output_fd = dup(STDOUT_FILENO);
        if (output_fd < 0) {
            send_error(error_pipe[1]);
            close(error_pipe[1]);
            _exit(1);
        }
        if (fcntl(output_fd, F_SETFD, FD_CLOEXEC) < 0) {
            close(output_fd);
            send_error(error_pipe[1]);
            close(error_pipe[1]);
            _exit(1);
        }
        if (infile != NULL) {
            int fd = open(infile, O_RDONLY);
            if (fd < 0) {
                report_invalid(output_fd);
                send_error(error_pipe[1]);
                close(output_fd);
                close(error_pipe[1]);
                _exit(1);
            }
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
        /* Output redirection. */
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
                send_error(error_pipe[1]);
                close(output_fd);
                close(error_pipe[1]);
                _exit(1);
            }
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
         * execvp failure must not be printed here because
         * stdout may already be redirected.
        */
        execvp(argv[0], argv);
        report_invalid(output_fd);
        send_error(error_pipe[1]);
        close(output_fd);
        close(error_pipe[1]);
        _exit(127);
    }
    close(error_pipe[1]);
    if (waitpid(pid, &status, 0) < 0) {
        close(error_pipe[0]);
        printf("Invalid Command\n");
        return 1;
    }
    char error;
    ssize_t bytes = read(error_pipe[0], &error, 1);
    close(error_pipe[0]);
    if (bytes > 0) {
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
}
int handle_cd(char *argv[], int argc, int error_fd) {
    char current_dir[MAX_INPUT_LEN + 1];
    /* cd and cd ~ */
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "~") == 0)) {
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            report_invalid(error_fd);
            return 1;
        }
        if (chdir(shell_home) != 0) {
            report_invalid(error_fd);
            return 1;
        }
        strcpy(previous_dir, current_dir);

        return 0;
    }
    /* cd - */
    if (argc == 2 && strcmp(argv[1], "-") == 0) {
        if (previous_dir[0] == '\0') {
            report_invalid(error_fd);
            return 1;
        }
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            report_invalid(error_fd);
            return 1;
        }
        if (chdir(previous_dir) != 0) {
            report_invalid(error_fd);
            return 1;
        }
        strcpy(previous_dir, current_dir);
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            report_invalid(error_fd);
            return 1;
        }
        printf("%s\n", current_dir);

        return 0;
    }
    /* cd path */
    if (argc == 2) {
        if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
            report_invalid(error_fd);
            return 1;
        }
        if (chdir(argv[1]) != 0) {
            report_invalid(error_fd);
            return 1;
        }
        strcpy(previous_dir, current_dir);
        return 0;
    }
    report_invalid(error_fd);
    return 1;
}
int handle_history(char *argv[], int argc, int error_fd) {
    char *end;
    long value;
    if (argc == 1) {
        print_history(-1);
        return 0;
    }
    if (argc == 2) {
        value = strtol(argv[1], &end, 10);
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
int execute_builtin_with_redirection(char *argv[], int argc) {
    char *infile = NULL;
    char *outfile = NULL;
    int append = 0;
    int redir_argc;
    int saved_stdin;
    int saved_stdout;
    int status;
    redir_argc = parse_redirections(argv, argc, &infile, &outfile, &append);
    if (redir_argc <= 0) {
        printf("Invalid Command\n");
        return 1;
    }
    saved_stdin = dup(STDIN_FILENO);
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdin < 0 || saved_stdout < 0) {
        if (saved_stdin >= 0) close(saved_stdin);
        if (saved_stdout >= 0) close(saved_stdout);
        printf("Invalid Command\n");
        return 1;
    }
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
    if (strcmp(argv[0], "history") == 0) {
        status = handle_history(argv, redir_argc, saved_stdout);
    }
    else if (strcmp(argv[0], "cd") == 0) {
        status = handle_cd(argv, redir_argc, saved_stdout);
    }
    else {
        dup2(saved_stdin, STDIN_FILENO);
        dup2(saved_stdout, STDOUT_FILENO);
        close(saved_stdin);
        close(saved_stdout);
        exit(0);
    }

    dup2(saved_stdin, STDIN_FILENO);
    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    return status;
}
int split_pipeline(char *line, char *commands[]) {
    int count = 0;
    char *start = line;
    while (1) {
        if (count >= MAX_PIPE_STAGES) {
            return -1;
        }
        commands[count++] = start;
        char *pipe_pos = strchr(start, '|');
        if (pipe_pos == NULL) {
            break;
        }
        *pipe_pos = '\0';
        start = pipe_pos + 1;
    }
    return count;
}
int execute_pipeline(char *commands[], int count) {
    int pipes[MAX_PIPE_STAGES - 1][2];
    int error_pipes[MAX_PIPE_STAGES][2];
    pid_t pids[MAX_PIPE_STAGES];
    /* Create normal pipes. */
    for (int i = 0; i < count - 1; i++) {
        if (pipe(pipes[i]) < 0) {
            printf("Invalid Command\n");
            return 1;
        }
    }
    for (int i = 0; i < count; i++) {
        if (pipe(error_pipes[i]) < 0) {
            printf("Invalid Command\n");
            return 1;
        }
    }
    /* Create children. */
    for (int i = 0; i < count; i++) {
        char *argv[MAX_ARGS + 1];
        char buffer[TOKEN_BUFFER_SIZE];
        int argc = tokenize(commands[i], argv, buffer);
        if (argc == 0) {
            pids[i] = -1;
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
        if (pids[i] == 0) {
            close(error_pipes[i][0]);
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
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
            }
            if (i < count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    report_invalid(output_fd);
                    send_error(error_pipes[i][1]);
                    close(output_fd);
                    close(error_pipes[i][1]);
                    _exit(1);
                }
            }
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
             * Close all normal pipe descriptors.
            */
            for (int j = 0; j < count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            if (strcmp(argv[0], "cd") == 0 || strcmp(argv[0], "history") == 0 || strcmp(argv[0], "exit") == 0) {
                report_invalid(output_fd);
                send_error(error_pipes[i][1]);
                close(output_fd);
                close(error_pipes[i][1]);
                _exit(1);
            }
            execvp(argv[0], argv);
            report_invalid(output_fd);
            send_error(error_pipes[i][1]);
            close(output_fd);
            close(error_pipes[i][1]);
            _exit(127);
        }
    }
    /*
     * Parent closes normal pipe descriptors.
    */
    for (int i = 0; i < count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    /*
     * Parent closes the write side of every
     * error pipe.
    */
    for (int i = 0; i < count; i++) {
        close(error_pipes[i][1]);
    }
    /*
     * Wait for every pipeline stage.
    */
    int last_status = 1;
    for (int i = 0; i < count; i++) {
        if (pids[i] <= 0) {
            close(error_pipes[i][0]);
            continue;
        }
        int status;
        if (waitpid(pids[i], &status, 0) < 0) {
            close(error_pipes[i][0]);
            continue;
        }
        char error;
        ssize_t bytes = read(error_pipes[i][0], &error, 1);
        close(error_pipes[i][0]);
        if (bytes > 0) {
            last_status = 1;
        }
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
int run_segment(char *segment){
    /*
     * Check pipeline is there or not.
    */
    if (strchr(segment, '|') != NULL) {
        char *commands[MAX_PIPE_STAGES];
        int count = split_pipeline(segment, commands);
        if (count < 1 || count > MAX_PIPE_STAGES) {
            printf("Invalid Command\n");
            return 1;
        }
        return execute_pipeline(commands, count);
    }
    char *argv[MAX_ARGS + 1];
    char buffer[TOKEN_BUFFER_SIZE];
    int argc = tokenize(segment, argv, buffer);
    if (argc == 0) {
        return 1;
    }
    /* Built-ins. */
    if (strcmp(argv[0], "cd") == 0 ||
        strcmp(argv[0], "history") == 0 ||
        strcmp(argv[0], "exit") == 0) {
        return execute_builtin_with_redirection(argv, argc);
    }
    /* External command. */
    return execute_command(argv);
}
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
        /*
         * || must be recognized as a separator,
         * not as a pipeline.
        */
        if (line[i] == '|' && line[i + 1] == '|') {
            *separator_length = 2;
            *separator_pos = &line[i];
            return 3;
        }
        /*
         * A single | is a pipeline, so we do not
         * treat it as a command separator.
        */
        i++;
    }
    *separator_length = 0;
    *separator_pos = NULL;
    return 0;
}
int execute_line(char *line) {
    char *current = line;
    int last_status = 0;
    int should_run = 1;
    while (*current != '\0') {
        int separator_length;
        char *separator_pos;
        int separator_type = find_separator(current, &separator_length, &separator_pos);
        char segment[MAX_INPUT_LEN + 1];
        /*
         * Copy command before separator.
        */
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
            strncpy(segment, current, MAX_INPUT_LEN);
            segment[MAX_INPUT_LEN] = '\0';
        }
        /*
         * Remove leading whitespace.
        */
        char *start = segment;
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        /*
         * Remove trailing whitespace.
        */
        char *end = start + strlen(start);
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        *end = '\0';
        /*
         * Execute this command if required.
        */
        if (*start != '\0' && should_run) {
            last_status = run_segment(start);
        }
        /*
         * No more separators.
        */
        if (separator_pos == NULL) {
            break;
        }
        /*
         * Move after the separator.
        */
        current = separator_pos + separator_length;
        /*
         * Decide whether next command runs.
        */
        if (separator_type == 1) {
            should_run = 1;
        }
        else if (separator_type == 2) {
            should_run = (last_status == 0);
        }
        else {
            should_run = (last_status != 0);
        }
    }
    return last_status;
}
int main(void) {
    char input[MAX_INPUT_LEN + 2];
    char history_line[MAX_INPUT_LEN + 1];
    if (getcwd(shell_home, sizeof(shell_home)) == NULL) {
        printf("Invalid Command\n");
        return 1;
    }
    while (1) {
        /*
         * Prompt.
        */
        printf("%s", PROMPT);
        fflush(stdout);
        /*
         * Read input.
        */
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        /*
         * Remove final newline.
        */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') {
            input[len - 1] = '\0';
        }
        /*
         * Ignore blank input.
        */
        int only_whitespace = 1;
        for (int i = 0; input[i] != '\0'; i++) {
            if (input[i] != ' ' && input[i] != '\t') {
                only_whitespace = 0;
                break;
            }
        }
        if (only_whitespace) {
            continue;
        }
        /*
         * Save the raw line before modifying it.
        */
        strcpy(history_line, input);
        add_history(history_line);
        execute_line(input);
    }
    return 0;
}