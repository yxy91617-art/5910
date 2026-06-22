# Project 2b

## Student Information
Name: Ethan Yang
Email: yangxiny@seas.upenn.edu
GitHub: yxy91617-art

## Overview
This submission implements `penn-sh` for CIT 5950 Project 2b. The shell supports command execution through `execvp`, standard input redirection with `<`, standard output redirection with `>`, and a two-stage pipeline with `|`.

The parser rejects invalid operator sequences, multiple conflicting redirections, multiple pipes, output redirection on the pipe writer, and input redirection on the pipe reader. Errors include the word `invalid` and the shell continues to accept later commands.

## Files
- `penn-sh.c`: shell loop, signal setup, single-command execution, two-stage pipeline execution, redirection application, and waiting.
- `command.c` / `command.h`: command input, tokenization, validation, pipe parsing, redirection parsing, and argument construction.
- `tokenizer.c` / `tokenizer.h`: tokenizer provided with the assignment.
- `Makefile`: builds the `penn-sh` executable.
