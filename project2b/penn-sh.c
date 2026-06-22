#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "command.h"

pid_t childPids[2] = {0, 0};

static void executeShell(void);
static void executeSingleCommand(Command *command);
static void executePipeline(Command *command);
static void registerSignalHandlers(void);
static void sigintHandler(int sig);
static void resetChildSignalHandler(Command *command);
static void redirectStandardInput(char *path);
static void redirectStandardOutput(char *path);
static void applyRedirections(CommandStage *stage);
static void waitForPid(pid_t pid);
static void closePipeEnd(int fd);
static void writeToStdout(char *text);

int main(void) {
    registerSignalHandlers();

    while (1) {
        executeShell();
    }

    return 0;
}

static void executeShell(void) {
    Command command;
    int status;

    writeToStdout("penn-sh> ");
    status = readCommand(&command);
    if (status < 0) {
        exit(EXIT_SUCCESS);
    }
    if (status == 0) {
        return;
    }

    if (command.has_pipe) {
        executePipeline(&command);
    } else {
        executeSingleCommand(&command);
    }

    freeCommand(&command);
}

static void executeSingleCommand(Command *command) {
    childPids[0] = fork();
    if (childPids[0] < 0) {
        perror("invalid fork");
        freeCommand(command);
        exit(EXIT_FAILURE);
    }

    if (childPids[0] == 0) {
        resetChildSignalHandler(command);
        applyRedirections(&command->stages[0]);
        execvp(command->stages[0].args[0], command->stages[0].args);
        perror("invalid command");
        freeCommand(command);
        exit(EXIT_FAILURE);
    }

    waitForPid(childPids[0]);
    childPids[0] = 0;
}

static void executePipeline(Command *command) {
    int pipeFds[2];

    if (pipe(pipeFds) < 0) {
        perror("invalid pipe");
        freeCommand(command);
        exit(EXIT_FAILURE);
    }

    childPids[0] = fork();
    if (childPids[0] < 0) {
        perror("invalid fork");
        closePipeEnd(pipeFds[0]);
        closePipeEnd(pipeFds[1]);
        freeCommand(command);
        exit(EXIT_FAILURE);
    }

    if (childPids[0] == 0) {
        resetChildSignalHandler(command);
        closePipeEnd(pipeFds[0]);
        applyRedirections(&command->stages[0]);
        if (dup2(pipeFds[1], STDOUT_FILENO) < 0) {
            perror("invalid pipe output");
            freeCommand(command);
            exit(EXIT_FAILURE);
        }
        closePipeEnd(pipeFds[1]);
        execvp(command->stages[0].args[0], command->stages[0].args);
        perror("invalid command");
        freeCommand(command);
        exit(EXIT_FAILURE);
    }

    childPids[1] = fork();
    if (childPids[1] < 0) {
        perror("invalid fork");
        closePipeEnd(pipeFds[0]);
        closePipeEnd(pipeFds[1]);
        kill(childPids[0], SIGTERM);
        waitForPid(childPids[0]);
        childPids[0] = 0;
        freeCommand(command);
        exit(EXIT_FAILURE);
    }

    if (childPids[1] == 0) {
        resetChildSignalHandler(command);
        closePipeEnd(pipeFds[1]);
        if (dup2(pipeFds[0], STDIN_FILENO) < 0) {
            perror("invalid pipe input");
            freeCommand(command);
            exit(EXIT_FAILURE);
        }
        closePipeEnd(pipeFds[0]);
        applyRedirections(&command->stages[1]);
        execvp(command->stages[1].args[0], command->stages[1].args);
        perror("invalid command");
        freeCommand(command);
        exit(EXIT_FAILURE);
    }

    closePipeEnd(pipeFds[0]);
    closePipeEnd(pipeFds[1]);
    waitForPid(childPids[0]);
    waitForPid(childPids[1]);
    childPids[0] = 0;
    childPids[1] = 0;
}

static void registerSignalHandlers(void) {
    if (signal(SIGINT, sigintHandler) == SIG_ERR) {
        perror("invalid signal");
        exit(EXIT_FAILURE);
    }
}

static void sigintHandler(int sig) {
    if (sig == SIGINT && childPids[0] == 0 && childPids[1] == 0) {
        write(STDOUT_FILENO, "\npenn-sh> ", 10);
    }
}

static void resetChildSignalHandler(Command *command) {
    if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
        perror("invalid signal");
        freeCommand(command);
        exit(EXIT_FAILURE);
    }
}

static void applyRedirections(CommandStage *stage) {
    if (stage->input_path != NULL) {
        redirectStandardInput(stage->input_path);
    }
    if (stage->output_path != NULL) {
        redirectStandardOutput(stage->output_path);
    }
}

static void redirectStandardInput(char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("invalid standard input redirect");
        exit(EXIT_FAILURE);
    }
    if (dup2(fd, STDIN_FILENO) < 0) {
        perror("invalid standard input redirect");
        close(fd);
        exit(EXIT_FAILURE);
    }
    if (close(fd) < 0) {
        perror("invalid close");
        exit(EXIT_FAILURE);
    }
}

static void redirectStandardOutput(char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("invalid standard output redirect");
        exit(EXIT_FAILURE);
    }
    if (dup2(fd, STDOUT_FILENO) < 0) {
        perror("invalid standard output redirect");
        close(fd);
        exit(EXIT_FAILURE);
    }
    if (close(fd) < 0) {
        perror("invalid close");
        exit(EXIT_FAILURE);
    }
}

static void waitForPid(pid_t pid) {
    int status;
    int result;

    result = waitpid(pid, &status, 0);
    while (result < 0 && errno == EINTR) {
        result = waitpid(pid, &status, 0);
    }
    if (result < 0) {
        perror("invalid wait");
        exit(EXIT_FAILURE);
    }
}

static void closePipeEnd(int fd) {
    if (close(fd) < 0) {
        perror("invalid close");
        exit(EXIT_FAILURE);
    }
}

static void writeToStdout(char *text) {
    if (write(STDOUT_FILENO, text, strlen(text)) == -1) {
        perror("invalid write");
        exit(EXIT_FAILURE);
    }
}
