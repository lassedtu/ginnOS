# The shell

The shell, `/bin/sh`, runs commands that you type. It runs in Mimir (the
terminal on the desktop) and on the console.

The code: [`src/user/sh/`](https://github.com/lassedtu/ginnOS/tree/main/src/user/sh)

The prompt shows the current folder:

```
skl:/home/user $
```

## Edit a command

| Key | Result |
| --- | --- |
| Left, Right | move the cursor |
| Home or Ctrl+A, End or Ctrl+E | go to the start or the end of the line |
| Backspace, Delete | delete the character before or at the cursor |
| Ctrl+K | delete from the cursor to the end of the line |
| Ctrl+U | delete from the start of the line to the cursor |
| Ctrl+W | delete the word before the cursor |
| Ctrl+L | clear the screen |
| Ctrl+C | drop the line and show a new prompt |
| Ctrl+D | stop the shell (only on an empty line) |
| Up, Down | the commands before, from the history |
| Enter | run the command |

A line can be longer than the terminal is wide. It then continues on the
next row.

The history keeps the last 128 commands. It does not keep a command two
times in a row. The `history` command shows the list.

## Run commands

The shell finds a command in the folders of `PATH`. The default `PATH`
is `/bin`.

Put spaces around `|`, `>`, `>>`, `<` and `&`. The shell splits a line
only at spaces, so `ls|wc` is one word.

| You type | Result |
| --- | --- |
| `ls -l /bin` | runs `ls` with two arguments |
| `ls > files.txt` | writes the output to `files.txt` |
| `ls >> files.txt` | adds the output to the end of `files.txt` |
| `sort < files.txt` | reads the input from `files.txt` |
| `sleep 60 &` | runs the command in the background |

A pipeline sends the output of one command into the next one:

```sh
$ ls /bin | wc -l
```

### Quotes

- `"two words"` is one word. `$` in double quotes still expands.
- `'two words'` is one word. The shell does not yet keep `$` in single
  quotes as it is: `'$HOME'` also expands.
- `\` before a character keeps that character as it is, for example
  `\ ` for a space.

### Variables

| You type | Result |
| --- | --- |
| `export NAME=value` | sets `NAME` for the shell and the commands it starts |
| `echo $NAME` or `echo ${NAME}` | the value of `NAME` |
| `echo $?` | the exit status of the last command |
| `echo $$` | the process ID of the shell |
| `unset NAME` | removes `NAME` |
| `env` or `printenv` | shows all variables |

## Jobs

Each pipeline is a job.

| Key or command | Result |
| --- | --- |
| Ctrl+C | stops the job in the foreground (`SIGINT`) |
| Ctrl+\ | stops the job in the foreground (`SIGQUIT`) |
| Ctrl+Z | pauses the job in the foreground (`SIGTSTP`) |
| `jobs` | lists the jobs, with their number |
| `fg %1` | continues job 1 in the foreground |
| `bg %1` | continues job 1 in the background |
| `kill %1`, `kill 42` | sends `SIGTERM` to job 1, or to process 42 |
| `kill -9 %1`, `kill -KILL %1` | sends `SIGKILL`; a signal is a number or a name, with or without `SIG` |

When a background job ends or pauses, the shell tells you before the
next prompt.

## Built-in commands

`cd`, `exit`, `history`, `export`, `unset`, `env`, `printenv`, `which`,
`jobs`, `fg`, `bg` and `kill` are in the shell. All other commands are
programs in `/bin`. `which <command>` tells which one a command is.

## Scripts

`sh -c '<command>'` runs one command line and stops. The shell also reads
commands from a pipe or a file, one line at a time:

```sh
$ sh < commands.txt
```

## What the shell cannot do yet

- `;`, `&&` and `||` between commands
- script files as an argument (`sh script.sh`) and `#!` lines
- `if`, `for`, `while`, `case` and functions
- file name patterns such as `*.c`
- `~` for the home folder (`cd` with no argument goes to `/`)
- `2>` and other redirections of a numbered file descriptor
- `$(command)` and `` `command` ``
- comments with `#`
- `NAME=value` without `export`
