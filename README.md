# Mini Shell in C

A small command shell I built from scratch in C, to understand how a terminal really works inside.

This README explains everything in simple words. You do not need to know anything before reading it.

---

## 1. What is a shell?

When you use a computer, you can click icons with a mouse. But there is another way: you can **type commands** and the computer does what you say.

The program that reads your typed commands and runs them is called a **shell**.

- **Terminal**: the window where you type.
- **Shell**: the program inside the terminal that understands what you type.
- **Bash**: a very popular shell used on Linux. My project is a small, simple version of bash.

Think of the shell like a waiter in a restaurant. You tell the waiter what you want (command), the waiter goes to the kitchen (the computer), and brings back the result.

---

## 2. Words you should know

| Word | Simple meaning |
|---|---|
| **Command** | An instruction you type, like `ls` (show files) or `echo hello` (print hello). |
| **Argument** | Extra information given to a command. In `echo hello`, the word `hello` is the argument. |
| **Prompt** | The small text shown before you type, so you know the shell is ready. |
| **Program** | A set of instructions saved on the computer. |
| **Process** | A program that is running right now. |
| **Child process** | A new process made by another process (its parent). Like a helper. |
| **fork** | A system call (a request to the computer's core) that makes a copy of the current process. The copy is the child. |
| **exec / execvp** | A system call that replaces a process with a new program. I use `execvp`, which also searches for the command by name. |
| **PATH** | A list of folders where the computer looks for commands. |
| **wait / waitpid** | A system call that makes the parent wait until its child finishes. |
| **Exit status** | A number a program gives when it ends. **0 means success**. Any other number means failure. |
| **Built-in command** | A command that the shell does by itself, without starting another program. (`cd`, `history`, `exit`) |
| **External command** | A command that is a separate program on the computer. (`ls`, `cat`, `grep`) |
| **Directory** | Another word for folder. |
| **Absolute path** | A full address of a folder from the start, like `/home/user/records`. |
| **Relative path** | An address from where you are now, like `records` or `..`. |
| **Standard input (stdin)** | Where a program reads its input from. Usually the keyboard. |
| **Standard output (stdout)** | Where a program writes its output. Usually the screen. |
| **File descriptor** | A small number the computer uses to keep track of an open file or pipe. For example 0 = input, 1 = output. |
| **Pipe** | A tube between two programs. What one writes, the other reads. |
| **Redirection** | Changing where input comes from, or where output goes (a file instead of the keyboard or screen). |
| **Tokenizing** | Cutting a line of text into small pieces (words). |
| **Parsing** | Reading and understanding the pieces of a line. |
| **System call** | A request that a program makes to the operating system (the main software of the computer). |

---

## 3. What my shell can do

### a) Run commands
You type a command, the shell runs it, shows the result, and then asks for the next one.

```
MTL458 > echo shell test
shell test
```

- Extra spaces are ignored. `echo    hi` works like `echo hi`.
- If you just press Enter, nothing happens and the prompt comes back.
- If the command does not exist, it prints `Invalid Command`.

```
MTL458 > nosuchcommand
Invalid Command
```

### b) Built-in commands

**`cd`** (change directory, which means go to another folder)

| You type | What happens |
|---|---|
| `cd` or `cd ~` | Go to the home folder |
| `cd -` | Go back to the last folder, and print its full address |
| `cd .` | Stay in the same folder |
| `cd ..` | Go one folder up |
| `cd records` | Go into the folder `records` |

If the folder does not exist, it prints `Invalid Command` and stays where it is.

**`history`**
- `history` shows every command typed so far (including `history` itself).
- `history 3` shows only the last 3 commands.
- It remembers up to 2048 commands. After that, the oldest one is removed.
- Each line is saved exactly as typed, even when it failed.
- Empty lines are not saved.

**`exit`**: closes the shell.

### c) Pipes `|`
A pipe sends the output of one command into the next command.

```
MTL458 > cat records/entry.txt | grep gamma | wc -l
1
```

Meaning: `cat` shows the file, `grep gamma` keeps only the lines with the word gamma, and `wc -l` counts the lines. I can join up to 3 commands. If one command in the pipe is wrong, it says `Invalid Command` for that one, and the others still run.

### d) Redirection `<`, `>`, `>>`

| Sign | Meaning |
|---|---|
| `<` | Take input from a file |
| `>` | Put output into a file (the old content is erased first) |
| `>>` | Add output at the end of a file (the old content stays) |

```
MTL458 > echo greetings > note_out.txt
MTL458 > cat note_out.txt
greetings
MTL458 > sort < data.txt > sorted.txt
```

- If the output file does not exist, it is created.
- The order of `<` and `>` can be any: both work.

### e) Joining commands in one line

| You type | What happens |
|---|---|
| `a ; b` | Run `a`, then run `b` always |
| `a && b` | Run `b` only if `a` worked (exit status 0) |
| `a \|\| b` | Run `b` only if `a` failed |

```
MTL458 > madeupcmd || echo recovered
Invalid Command
recovered
```

A command that is not found always counts as a failure.

---

## 4. How it works inside (step by step)

My shell does the same loop again and again:

1. **Show the prompt** and wait for me to type a line.
2. **Read the line.** If it is empty, go back to step 1.
3. **Save the line in history** exactly as typed.
4. **Split the line** at `;`, `&&` and `||` into small commands. A single `|` is not a split, it is a pipe.
5. For each small command, **decide if it should run**, using the exit status of the one before it.
6. **Cut the command into words** (tokenizing) and find any `<`, `>` or `>>` (parsing).
7. **If it is `cd`, `history` or `exit`**, the shell does it by itself.
8. **If it is a pipe**, the shell makes pipes and one child process for each command.
9. **Otherwise**, the shell does these three steps:
   - `fork`: make a child process.
   - `execvp`: the child becomes the command (after setting up any file redirection).
   - `waitpid`: the shell waits and gets the exit status.
10. Go back to step 1.

### A small trick I used
When `execvp` fails in the child (command not found), the child cannot easily tell the parent. So I made a tiny extra pipe. The child writes one byte into it when something goes wrong, and the parent reads it. This way, the parent knows the command failed and `&&` and `||` work correctly.

### Taking care of pipes
Every pipe has two ends: one to write and one to read. If a process keeps an end it does not need, the reading side waits forever. So I close all unused pipe ends in the parent and in every child.

---

## 5. Rules and limits
- One line can have at most 2048 characters and 100 words.
- A pipe can join at most 3 commands.
- One command can have at most one `<` and one `>` (or `>>`).
- Brackets `( )` are not supported.
- The special signs `| & ; < >` cannot be used inside names.
- `cd`, `history` and `exit` cannot be used inside a pipe.
- The only error message the shell prints is `Invalid Command`.

---

## 6. What I learned
- How a shell works: read, understand, run, repeat.
- How `fork`, `exec` and `wait` work together to run any program.
- How pipes connect programs, and why closing unused ends matters.
- How redirection works by changing file descriptors.
- How exit status (0 or not 0) decides what runs next in `&&` and `||`.
- How to cut text into words and read it by hand in C.

## 7. Made with
The C language and Linux system calls: `fork`, `execvp`, `waitpid`, `pipe`, `dup2`, `open`, `read`, `write`, `chdir`.
