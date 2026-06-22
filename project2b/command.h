#ifndef COMMAND_H
#define COMMAND_H

#define INPUT_SIZE 1024
#define MAX_TOKENS 512
#define MAX_ARGS 512

typedef struct command_stage {
    char *args[MAX_ARGS];
    char *input_path;
    char *output_path;
    int arg_count;
} CommandStage;

typedef struct command {
    char *tokens[MAX_TOKENS];
    int token_count;
    int has_pipe;
    CommandStage stages[2];
} Command;

void initCommand(Command *command);
int readCommand(Command *command);
void freeCommand(Command *command);

#endif
