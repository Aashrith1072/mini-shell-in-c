# Mini Shell in C

A small command shell I built from scratch in C. I was curious how a terminal (like bash) really works inside, so I made my own.

It shows a prompt, reads what I type, and runs it.

## What it can do

**Run any command**
- Commands like `ls`, `echo`, `cat`, `grep` all work.
- It makes a child process (a copy of the program) with `fork`, runs the command in it with `execvp`, and waits with `waitpid`.
- Extra spaces are ignored. An empty line does nothing.
- If a command is not found, it prints `Invalid Command`.

**Built-in commands** (made inside the shell itself)
- `cd` : go home. Also works with `~`, `-` (go back to the last folder), `.`, `..` and any path.
- `history` : shows all commands typed so far. `history n` shows the last `n`.
- `exit` : closes the shell.

**Pipes `|`**
- Send the output of one command into the next one.
- Works with up to 3 commands. Example: `cat data.txt | grep gamma | wc -l`

**Redirection** (use files instead of the screen or keyboard)
- `<` reads input from a file.
- `>` writes output to a file (clears the old content).
- `>>` adds output to the end of a file.

**Joining commands**
- `a ; b` runs both, one after the other.
- `a && b` runs `b` only if `a` worked.
- `a || b` runs `b` only if `a` failed.

## Small things I took care of
- Closing every pipe end I do not need, so the shell never hangs.
- Checking the exit status (0 means success) to decide `&&` and `||`.
- Saving each command in history exactly as I typed it, even if it failed.
- The shell should never crash, and it always comes back to the prompt.

## What I learned
- How `fork`, `exec` and `wait` work together.
- How pipes and file redirection really work using file descriptors (small numbers that stand for open files).
- How to split a line of text into commands and arguments by hand.
- Why closing unused pipe ends is so important.

## Made with
C and standard Linux system calls (`fork`, `execvp`, `waitpid`, `pipe`, `dup2`, `open`).
