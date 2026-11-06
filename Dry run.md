# Dry Run of My Mini Shell
## What each function does 

| Function | What it does |
|---|---|
| `main` | The boss. Shows the prompt, reads your line, saves it in history, and runs it. Repeats until you type `exit`. |
| `execute_line` | Cuts the line at `;` `&&` `\|\|` and runs the small commands one by one. Decides if the next one should run. |
| `find_separator` | Looks in the line and finds the first `;`, `&&` or `\|\|`. A single `\|` is ignored, because it is a pipe. |
| `run_segment` | Looks at one small command and chooses how to run it: pipe, built-in, or normal command. |
| `tokenize` | Cuts a command into words. Example: `echo   hi` becomes `echo`, `hi`. Also makes `<` `>` `>>` separate words. |
| `parse_redirections` | Finds `<` `>` `>>` and the file names. Removes them from the words, so only the real command stays. |
| `execute_command` | Runs a normal command like `ls` or `cat`. Makes a child (`fork`), turns it into the command (`execvp`), and waits (`waitpid`). |
| `execute_builtin_with_redirection` | Runs `cd`, `history` or `exit`. Also supports `>` and `<` for them. |
| `handle_cd` | Does `cd`. Moves to the folder with `chdir` and remembers the old folder for `cd -`. |
| `handle_history` | Does `history` and `history n`. Checks that `n` is a good number, then prints. |
| `add_history` | Saves one typed line in the history list. Removes the oldest line when 2048 are full. |
| `print_history` | Prints the last `n` lines of the history list. |
| `split_pipeline` | Cuts a line at every `\|`, so one line becomes separate commands (maximum 3). |
| `execute_pipeline` | Runs commands joined by pipes. Makes the tubes, one child for each command, and connects them. |
| `report_invalid` | Prints `Invalid Command` on the real screen. |
| `send_error` | Child sends one small byte to the parent to say "I failed". |

**The path of one line:**
```
main -> execute_line -> run_segment -> execute_command (normal command)
                                    -> execute_builtin_with_redirection (cd / history / exit)
                                    -> execute_pipeline (if there is a |)
```

---

## Example 1: `echo   hello  world`

(a normal command with extra spaces)

**Step 1: `main`**
- `fgets` reads the line.
- The line is not empty, so `add_history` saves it.
- `execute_line` is called.

**Step 2: `execute_line`**
- `find_separator` finds no `;` `&&` `||`, so `separator_pos = NULL`.
- The whole line becomes one `segment`, and `run_segment` is called.

**Step 3: `run_segment`**
- There is no `|`, so it calls `tokenize`.

**Step 4: `tokenize`** cuts the line into words:

| Step | What it sees | `argv` becomes |
|---|---|---|
| 1 | word `echo` | `argv[0] = "echo"` |
| 2 | skips 3 spaces, word `hello` | `argv[1] = "hello"` |
| 3 | skips 2 spaces, word `world` | `argv[2] = "world"` |
| end | line finished | `argv[3] = NULL`, `argc = 3` |

- `echo` is not `cd`, `history` or `exit`, so `execute_command` is called.

**Step 5: `execute_command`**
- `parse_redirections` finds no `<` `>` `>>`, so the words stay the same.
- `pipe(error_pipe)` makes the "I failed" tube.
- `fork()` makes a child.
- **Child:** `execvp("echo", argv)` turns it into `echo`. It prints `hello world`.
- **Parent:** `waitpid` waits. Then `read(error_pipe[0])` finds **nothing** (0 bytes), so there was no failure. The exit status is 0, so it returns 0.

**Output:**
```
hello world
```

---

## Example 2: `sort<data.txt>out.txt`

(redirection with no spaces)

**`tokenize`** makes the signs separate words:

| `argv` place | Value |
|---|---|
| 0 | `sort` |
| 1 | `<` |
| 2 | `data.txt` |
| 3 | `>` |
| 4 | `out.txt` |

**`parse_redirections`** goes through the list:

| i | Word | What happens |
|---|---|---|
| 0 | `sort` | normal word, kept |
| 1 | `<` | next word is `data.txt`, so `infile = "data.txt"`, skip it |
| 3 | `>` | next word is `out.txt`, so `outfile = "out.txt"`, `append = 0`, skip it |

After this, `argv = {"sort", NULL}` and it returns 1.

**In the child (`execute_command`):**
1. `open("data.txt", O_RDONLY)`, then `dup2(fd, STDIN_FILENO)`. Now `sort` reads from `data.txt`.
2. `open("out.txt", O_WRONLY | O_CREAT | O_TRUNC)`, then `dup2(fd, STDOUT_FILENO)`. Now `sort` writes into `out.txt`.
3. `execvp("sort", argv)`.

**Output on screen:** nothing. The sorted text is now inside `out.txt`.

(With `>>`, `append = 1`, so `O_APPEND` is used and the old text stays.)

---

## Example 3: `madeupcmd || echo recovered`

(a failed command and `||`)

**`execute_line`:**

| Step | What happens | Variables |
|---|---|---|
| 1 | `find_separator` finds `\|\|` | `separator_type = 3`, `length = 2` |
| 2 | Cuts the first part and trims it | `segment = "madeupcmd"` |
| 3 | `should_run` is 1, so `run_segment` runs it | |

**Running `madeupcmd` (`execute_command`):**
- Child: `execvp("madeupcmd", ...)` **fails**, because there is no such program.
- Child runs `report_invalid(output_fd)` and prints `Invalid Command`.
- Child runs `send_error`, which writes 1 byte into the error tube, and exits with 127.
- Parent: `read` gets **1 byte**, so it returns 1 (failed). `last_status = 1`.

**Back in `execute_line`:**

| Step | What happens | Variables |
|---|---|---|
| 4 | `current` moves after `\|\|` | rest = `" echo recovered"` |
| 5 | type is 3, so `should_run = (last_status != 0)` | `(1 != 0)` is true, so `should_run = 1` |
| 6 | Runs `echo recovered` | prints `recovered`, `last_status = 0` |

**Output:**
```
Invalid Command
recovered
```

**Same line with `&&`:** `madeupcmd && echo go`
- For `&&`, `should_run = (last_status == 0)`, which is `(1 == 0)`, so false.
- `echo go` is **skipped**. Only `Invalid Command` shows.

---

## Example 4: `cat data.txt | grep gamma`

(a pipe)

**`execute_line`:** `find_separator` sees a single `|`, so it ignores it. The whole line goes to `run_segment`.

**`run_segment`:** it finds `|`, so it calls `split_pipeline`.

**`split_pipeline`:**
- It puts an end mark where the `|` is.
- `commands[0] = "cat data.txt "` and `commands[1] = " grep gamma"`.
- It returns `count = 2`.

**`execute_pipeline(count = 2)`:**
1. `pipe(pipes[0])` makes one tube between the commands. It has a **read end** `[0]` and a **write end** `[1]`.
2. Error tubes are made for both commands.
3. **Child 0 (`cat`):** `i = 0` and `i < count - 1`, so `dup2(pipes[0][1], STDOUT)`. Its output goes **into the tube**. It closes all tube ends and runs `execvp("cat", ...)`.
4. **Child 1 (`grep`):** `i = 1`, so `dup2(pipes[0][0], STDIN)`. Its input comes **from the tube**. Its output stays on the screen. It closes all tube ends and runs `execvp("grep", ...)`.
5. **Parent:** closes both ends of the tube (it does not use them). Then it waits for both children. The last command (`grep`) decides the final status.

**The flow:**
```
data.txt -> cat -> [tube] -> grep gamma -> screen
```

**Output** (`data.txt` has `gamma delta`):
```
gamma delta
```

**Why closing tube ends matters:** `grep` stops only when the tube's write end is closed everywhere (it gets "end of file"). If any process keeps a write end open, `grep` waits forever.

---

## Example 5: `cd` commands

Say the shell started in `/home/user`, so `shell_home = "/home/user"`.

**Command 1: `cd records`**
- `run_segment` sees `cd`, so it goes to `execute_builtin_with_redirection`, then `handle_cd`.
- `argv = {"cd", "records"}`, `argc = 2`. It is not `~` and not `-`, so Case 3 runs.
- `getcwd` gives `current_dir = "/home/user"`.
- `chdir("records")` works.
- `previous_dir = "/home/user"`. Returns 0.

**Command 2: `cd -`**
- `argv[1] = "-"` and `previous_dir` is not empty.
- `current_dir = "/home/user/records"` (from `getcwd`).
- `chdir("/home/user")` works.
- `previous_dir = "/home/user/records"` (the old "now" becomes "previous").
- `getcwd` gives `/home/user`, and it prints:
```
/home/user
```

**Command 3: `cd nofolder`**
- `chdir("nofolder")` fails (returns not 0).
- `report_invalid(error_fd)` prints `Invalid Command`. Returns 1. The folder does not change.

**Command 4: `cd a b`**
- `argc = 3`. No case matches, so it falls to the end: `Invalid Command`.

---

## Example 6: `history`

Say you typed these 3 lines:
```
pwd
echo hi
history 2
```

**What `add_history` saved** (it saves **before** running, so `history 2` itself is included):

| Place | Saved line |
|---|---|
| `history[0]` | `pwd` |
| `history[1]` | `echo hi` |
| `history[2]` | `history 2` |

`history_count = 3`.

**`handle_history`:**
- `argc = 2`, so `strtol("2")` gives `value = 2`, and `*end == '\0'` (the text was a clean number).
- `print_history(2)`: `n = 2`, `start = 3 - 2 = 1`. It prints lines 1 and 2.

**Output:**
```
echo hi
history 2
```

**Wrong input:** `history abc`
- `strtol("abc")` stops at `a`, so `*end != '\0'`. The result is `Invalid Command`.

---

## Example 7: all together

**Line:** `mkdir box ; cd box && echo hi > a.txt`

| Step | Function | What happens | `last_status` |
|---|---|---|---|
| 1 | `find_separator` | finds `;` (type 1) | |
| 2 | `run_segment("mkdir box")` | `execute_command`: fork, exec `mkdir`. Folder made. | 0 |
| 3 | `execute_line` | `;` means always run, so `should_run = 1` | |
| 4 | `find_separator` | in the rest, finds `&&` (type 2) | |
| 5 | `run_segment("cd box")` | `handle_cd` runs `chdir("box")`. Now inside `box`. | 0 |
| 6 | `execute_line` | `&&` means `should_run = (0 == 0)`, so true | |
| 7 | `find_separator` | finds nothing (`>` is not a separator) | |
| 8 | `run_segment("echo hi > a.txt")` | `tokenize` gives `{"echo","hi",">","a.txt"}` | |
| 9 | `parse_redirections` | `argv = {"echo","hi"}`, `outfile = "a.txt"` | |
| 10 | child in `execute_command` | opens `a.txt`, `dup2` to output, `execvp("echo")` | 0 |

**Result:** the folder `box` exists, you are inside it, and `a.txt` contains `hi`. Nothing is printed on the screen.

Check it:
```
ownshell > cat a.txt
hi
```

---

## Quick memory table

| Function | Easy picture |
|---|---|
| `tokenize` | cuts text into words |
| `parse_redirections` | pulls out `<` `>` `>>` and file names |
| `execute_command` | fork, then exec, then wait |
| `execute_pipeline` | tubes plus many children |
| `execute_line` | decides if the next command runs |
| `handle_cd` | `getcwd`, then `chdir`, and remembers the old folder |
| `handle_history` | checks the number, then prints the last n lines |
