#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "command.h"
#include "tokenizer.h"

#define READ_EOF -1
#define READ_EMPTY 0
#define READ_LINE 1

static void writeToStderr(char *text);
static void initCommandStage(CommandStage *stage);
static int readInputLine(char *buffer);
static int tokenizeLine(Command *command, char *line);
static int parseTokens(Command *command);
static int addArgument(CommandStage *stage, char *token);
static char *stripMatchingQuotes(char *token);
static int isInputRedirect(char *token);
static int isOutputRedirect(char *token);
static int isPipe(char *token);
static int isUnsupportedOperator(char *token);
static int isOperator(char *token);

void initCommand(Command *command) {
    command->token_count = 0;
    command->has_pipe = 0;
    initCommandStage(&command->stages[0]);
    initCommandStage(&command->stages[1]);
}

int readCommand(Command *command) {
    char line[INPUT_SIZE];
    int status;

    initCommand(command);
    status = readInputLine(line);
    if (status == READ_EOF) {
        return READ_EOF;
    }
    if (status == READ_EMPTY) {
        return READ_EMPTY;
    }
    if (!tokenizeLine(command, line)) {
        freeCommand(command);
        return READ_EMPTY;
    }
    if (!parseTokens(command)) {
        freeCommand(command);
        return READ_EMPTY;
    }
    if (command->stages[0].arg_count == 0) {
        writeToStderr("invalid command\n");
        freeCommand(command);
        return READ_EMPTY;
    }

    return READ_LINE;
}

void freeCommand(Command *command) {
    int index;

    for (index = 0; index < command->token_count; index++) {
        free(command->tokens[index]);
        command->tokens[index] = NULL;
    }
    initCommand(command);
}

static void initCommandStage(CommandStage *stage) {
    stage->input_path = NULL;
    stage->output_path = NULL;
    stage->arg_count = 0;
    stage->args[0] = NULL;
}

static int readInputLine(char *buffer) {
    int bytes_read = 0;
    int reached_end = 0;

    while (bytes_read < INPUT_SIZE - 1) {
        char current;
        int result = read(STDIN_FILENO, &current, 1);

        if (result < 0) {
            if (errno == EINTR) {
                buffer[0] = '\0';
                return READ_EMPTY;
            }
            perror("invalid read");
            exit(EXIT_FAILURE);
        }
        if (result == 0) {
            if (bytes_read == 0) {
                return READ_EOF;
            }
            reached_end = 1;
            break;
        }
        if (current == '\n') {
            reached_end = 1;
            break;
        }

        buffer[bytes_read] = current;
        bytes_read++;
    }

    if (!reached_end && bytes_read == INPUT_SIZE - 1) {
        char ignored;
        int result = read(STDIN_FILENO, &ignored, 1);

        while (result > 0 && ignored != '\n') {
            result = read(STDIN_FILENO, &ignored, 1);
        }
        if (result < 0) {
            perror("invalid read");
            exit(EXIT_FAILURE);
        }
        writeToStderr("invalid command too long\n");
        return READ_EMPTY;
    }

    buffer[bytes_read] = '\0';
    if (bytes_read == 0) {
        return READ_EMPTY;
    }

    return READ_LINE;
}

static int tokenizeLine(Command *command, char *line) {
    TOKENIZER *tokenizer;
    char *token;

    tokenizer = init_tokenizer(line);
    while ((token = get_next_token(tokenizer)) != NULL) {
        if (command->token_count >= MAX_TOKENS - 1) {
            free(token);
            free_tokenizer(tokenizer);
            writeToStderr("invalid command too long\n");
            return 0;
        }
        command->tokens[command->token_count] = token;
        command->token_count++;
    }
    free_tokenizer(tokenizer);

    return command->token_count > 0;
}

static int parseTokens(Command *command) {
    int index;
    int stage_index = 0;
    CommandStage *stage = &command->stages[0];

    for (index = 0; index < command->token_count; index++) {
        char *token = command->tokens[index];

        if (isPipe(token)) {
            if (command->has_pipe) {
                writeToStderr("invalid multiple pipes\n");
                return 0;
            }
            if (stage->arg_count == 0) {
                writeToStderr("invalid pipe\n");
                return 0;
            }
            if (stage->output_path != NULL) {
                writeToStderr("invalid output redirection\n");
                return 0;
            }
            if (index + 1 >= command->token_count ||
                isOperator(command->tokens[index + 1])) {
                writeToStderr("invalid pipe\n");
                return 0;
            }
            command->has_pipe = 1;
            stage_index = 1;
            stage = &command->stages[1];
        } else if (isInputRedirect(token)) {
            if (stage->input_path != NULL) {
                writeToStderr("invalid multiple standard input redirects\n");
                return 0;
            }
            if (command->has_pipe && stage_index == 1) {
                writeToStderr("invalid input redirection\n");
                return 0;
            }
            if (index + 1 >= command->token_count ||
                isOperator(command->tokens[index + 1])) {
                writeToStderr("invalid standard input redirect\n");
                return 0;
            }
            stage->input_path = stripMatchingQuotes(command->tokens[index + 1]);
            index++;
        } else if (isOutputRedirect(token)) {
            if (stage->output_path != NULL) {
                writeToStderr("invalid multiple standard output redirects\n");
                return 0;
            }
            if (command->has_pipe && stage_index == 0) {
                writeToStderr("invalid output redirection\n");
                return 0;
            }
            if (index + 1 >= command->token_count ||
                isOperator(command->tokens[index + 1])) {
                writeToStderr("invalid standard output redirect\n");
                return 0;
            }
            stage->output_path = stripMatchingQuotes(command->tokens[index + 1]);
            index++;
        } else if (isUnsupportedOperator(token)) {
            writeToStderr("invalid unsupported operator\n");
            return 0;
        } else {
            if (!addArgument(stage, token)) {
                return 0;
            }
        }
    }

    if (command->has_pipe) {
        if (command->stages[1].arg_count == 0) {
            writeToStderr("invalid pipe\n");
            return 0;
        }
        if (command->stages[0].output_path != NULL) {
            writeToStderr("invalid output redirection\n");
            return 0;
        }
        if (command->stages[1].input_path != NULL) {
            writeToStderr("invalid input redirection\n");
            return 0;
        }
    }

    return 1;
}

static int addArgument(CommandStage *stage, char *token) {
    if (stage->arg_count >= MAX_ARGS - 1) {
        writeToStderr("invalid too many arguments\n");
        return 0;
    }
    stage->args[stage->arg_count] = stripMatchingQuotes(token);
    stage->arg_count++;
    stage->args[stage->arg_count] = NULL;
    return 1;
}

static char *stripMatchingQuotes(char *token) {
    int length = strlen(token);
    int index;
    char quote;

    if (length < 2) {
        return token;
    }

    quote = token[0];
    if ((quote != '\'' && quote != '"') || token[length - 1] != quote) {
        return token;
    }

    for (index = 1; index < length - 1; index++) {
        token[index - 1] = token[index];
    }
    token[length - 2] = '\0';
    return token;
}

static int isInputRedirect(char *token) {
    return strcmp(token, "<") == 0;
}

static int isOutputRedirect(char *token) {
    return strcmp(token, ">") == 0;
}

static int isPipe(char *token) {
    return strcmp(token, "|") == 0;
}

static int isUnsupportedOperator(char *token) {
    return strcmp(token, "&") == 0;
}

static int isOperator(char *token) {
    return isInputRedirect(token) || isOutputRedirect(token) || isPipe(token) ||
           isUnsupportedOperator(token);
}

static void writeToStderr(char *text) {
    if (write(STDERR_FILENO, text, strlen(text)) == -1) {
        exit(EXIT_FAILURE);
    }
}
