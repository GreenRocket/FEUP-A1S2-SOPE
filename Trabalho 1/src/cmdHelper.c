#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h> //wait
#include <unistd.h>   //pipe
#include "cmdHelper.h"

#define BUFFER_SIZE 512

int runCmd(char *cmdArgv[], int cmdArgc)
{
    int infoPipe[2];
    if (pipe(infoPipe) == -1)
    {
        perror("pipe() error");
        return -1;
    }
    // int retval = fcntl(infoPipe[0], F_SETFL, fcntl(infoPipe[0], F_GETFL) | O_NONBLOCK);
    // printf("Ret from fcntl: %d\n", retval);

    // Fazer fork() para chamar o comando file
    pid_t pid;
    if ((pid = fork()) < 0) // Ocorreu um erro
    {
        perror("fork() error");
        return -1;
    }
    else if (pid == 0) // Corre apenas no processo filho
    {
        // Abrir o "ficheiro" pipe-read
        // FILE *infoFile = fdopen(infoPipe[0], "r");
        // if (infoFile == NULL)
        // {
        //     perror("fdopen() error");
        //     return -1;
        // }

        // char buf[10];

        // int len = read(infoPipe[0], buf, sizeof(buf) - 1);
        // if (len < 0)
        // {
        //     perror("read error");

        //     char indentLevel = '2';
        //     int len = write(infoPipe[1], &indentLevel, 1);
        //     if (len < 0)
        //     {
        //         perror("write error");
        //     }
        //     else
        //     {
        //         printf("Buffer sent by %s: level %c\n", cmdArgv[cmdArgc - 1], indentLevel);
        //     }
        // }
        // else
        // {
        //     buf[len] = 0;
        //     printf("Buffer received by %s: %s\n", cmdArgv[cmdArgc - 1], buf);
        // }

        // char *line = NULL;
        // size_t len = 0;

        // if (getline(&line, &len, infoFile) != -1)
        // {
        //     printf("%s", line);
        // }

        // if (line)
        //     free(line);
        //fclose(infoFile);

        // Chamar o comando "file" com o execlp
        if (execvp(cmdArgv[0], cmdArgv) == -1)
        {
            perror("execvp() error");
            return -1;
        }
        //exit(EXIT_SUCCESS); // Aconteça erros ou não, a execução do filho acaba aqui.
    }
    // for (int i = 0; i < cmdArgc; ++i)
    //     free(cmdArgv[i]);
    free(cmdArgv[cmdArgc - 1]);

    // Execução do pai continua aqui.
    // Esperamos até que o processo filho acabe de correr.
    waitpid(pid, NULL, 0);

    return 0;
}

int routeCmd(char *cmdArgv[], int *PIPEREAD_FILENO)
{
    // Criar pipe para ligar file a forensic
    // Ao chamar pipe(), são adicionados file descriptors à file table
    // 0 - stdin, 1 - stdout, 2 - stderr, 3 - pipeIn[0] (read), 4 - pipeIn[1] (write)
    int pipeIn[2];
    if (pipe(pipeIn) == -1)
    {
        perror("pipe() error");
        return -1;
    }

    // Fazer fork() para chamar o comando file
    pid_t pid;
    if ((pid = fork()) < 0) // Ocorreu um erro
    {
        perror("fork() error");
        return -1;
    }
    else if (pid == 0) // Corre apenas no processo filho
    {
        // Os fd abertos são passados para o filho, ou seja, temos uma ligação entre o processo pai e filho.
        // É preciso usar o dup2() para ligar o output do filho à saída write do pipe.
        dup2(pipeIn[1], STDOUT_FILENO);
        // É também preciso fechar os fd não usados. Caso contrário uma futura operação de leitura encrava por ter o write aberto.
        close(pipeIn[0]); // Fechar fd read. Filho nunca vai ler do pipe.
        close(pipeIn[1]); // Fechar fd write. Só o stdout é que vai escrever para o pipe.

        // Chamar o comando "file" com o execlp
        if (execvp(cmdArgv[0], cmdArgv) == -1)
        {
            perror("execvp() error");
            return -1;
        }
        //exit(EXIT_SUCCESS); // Aconteça erros ou não, a execução do filho acaba aqui.
    }

    // Execução do pai continua aqui.
    // Só vai ler do pipe, logo fechamos o fd write.
    close(pipeIn[1]);

    // Esperamos até que o processo filho acabe de correr.
    waitpid(pid, NULL, 0);

    // Alterar pointer fornecido para o fd read.
    *PIPEREAD_FILENO = pipeIn[0];

    return 0;
}

int readRoutedCmdOutput(char **buffer, int PIPEREAD_FILENO)
{
    // Abrir o "ficheiro" pipe-read
    FILE *fileOutput = fdopen(PIPEREAD_FILENO, "r");
    if (fileOutput == NULL)
    {
        perror("fdopen() error");
        return -1;
    }

    // Ler do pipe em blocos de tamanho BUFFER_SIZE
    // fazendo realloc para cada bloco
    char tempBuffer[BUFFER_SIZE];
    *buffer = calloc(1, 0);
    while (fgets(tempBuffer, BUFFER_SIZE, fileOutput) != NULL)
    {
        *buffer = realloc(*buffer, strlen(*buffer) + strlen(tempBuffer) + 1);
        sprintf(*buffer, "%s%s", *buffer, tempBuffer);
    }

    // Fechar ficheiro e pipe
    fclose(fileOutput);
    close(PIPEREAD_FILENO);

    return 0;
}