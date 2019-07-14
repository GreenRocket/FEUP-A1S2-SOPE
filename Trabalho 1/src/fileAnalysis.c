#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "cmdHelper.h"
#include "fileAnalysis.h"

int checkPathType(const char *path)
{
    struct stat buf;
    stat(path, &buf);
    if (S_ISREG(buf.st_mode))
        return 0;
    if (S_ISDIR(buf.st_mode))
        return 1;

    printf("%s\n", path);
    return -1;
}

int GetFormattedDate(struct tm *ts, char **formattedDate)
{
    *formattedDate = malloc(19);
    if (sprintf(*formattedDate, "%d-%d-%dT%d:%d:%d", ts->tm_year + 1900, ts->tm_mon, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec) < 0)
    {
        perror("sprintf() error: ");
        return -1;
    }
    return 0;
}

int getFileCmdInfo(char **buffer, char *targetLocation)
{
    int PIPEREAD_FILENO;
    char *fileArgs[3] = {"file", targetLocation, NULL};
    if (routeCmd(fileArgs, &PIPEREAD_FILENO) != 0)
    {
        printf("Error running file command!\n");
        return -1;
    }

    if (readRoutedCmdOutput(buffer, PIPEREAD_FILENO) != 0)
    {
        printf("Error reading file command output!\n");
        return -1;
    }

    if (sscanf(*buffer, "%*[^:]: %[^,\n]", *buffer) == EOF)
    {
        perror("sscanf() error");
        return -1;
    }

    return 0;
}

int getStatCmdInfo(char **buffer, char *targetLocation)
{
    struct stat fileStat;
    if (stat(targetLocation, &fileStat) == -1)
    {
        perror("stat() error");
        return -1;
    }

    char *atimeStr;
    char *ctimeStr;
    char *mtimeStr;
    GetFormattedDate(localtime(&fileStat.st_atime), &atimeStr);
    GetFormattedDate(localtime(&fileStat.st_ctime), &ctimeStr);
    GetFormattedDate(localtime(&fileStat.st_mtime), &mtimeStr);

    size_t sizeLength = (fileStat.st_size > 0) ? ((int)(log10(fileStat.st_size) + 1)) : 1;

    *buffer = malloc(sizeLength + 10 + 57 + 4 + 1); // len(size) + len(permissions) + 4 * ',' + NULL terminator
    sprintf(*buffer, "%ld,%s%s%s%s%s%s%s%s%s%s,%s,%s,%s",
            fileStat.st_size,
            (S_ISDIR(fileStat.st_mode)) ? "d" : "-",
            (fileStat.st_mode & S_IRUSR) ? "r" : "-",
            (fileStat.st_mode & S_IWUSR) ? "w" : "-",
            (fileStat.st_mode & S_IXUSR) ? "x" : "-",

            (fileStat.st_mode & S_IRGRP) ? "r" : "-",
            (fileStat.st_mode & S_IWGRP) ? "w" : "-",
            (fileStat.st_mode & S_IXGRP) ? "x" : "-",

            (fileStat.st_mode & S_IROTH) ? "r" : "-",
            (fileStat.st_mode & S_IWOTH) ? "w" : "-",
            (fileStat.st_mode & S_IXOTH) ? "x" : "-",
            atimeStr,
            ctimeStr,
            mtimeStr);
    return 0;
}

int calculateHash(char **buffer, char *hashCmd, char *targetLocation)
{
    int PIPEREAD_FILENO;
    char *hashArgs[3] = {hashCmd, targetLocation, NULL};

    if (routeCmd(hashArgs, &PIPEREAD_FILENO) != 0)
    {
        printf("Error running hash command!\n");
        return -1;
    }

    if (readRoutedCmdOutput(buffer, PIPEREAD_FILENO) != 0)
    {
        printf("Error reading hash command output!\n");
        return -1;
    }

    if (sscanf(*buffer, "%[^ ]", *buffer) == EOF)
    {
        perror("sscanf() error");
        return -1;
    }

    return 0;
}

int processHashes(char **buffer, char *hashFunctions, char *targetLocation)
{
    char hashCmd[10];
    size_t doneFirst = 0;

    char *cpy = malloc(strlen(hashFunctions) + 1);
    strcpy(cpy, hashFunctions);

    char *ptr = strtok(cpy, ",");
    while (ptr != NULL)
    {
        if (strcmp(ptr, "md5") == 0 || strcmp(ptr, "sha1") == 0 || strcmp(ptr, "sha256") == 0)
        {
            char *tempBuf = NULL;
            sprintf(hashCmd, "%s%s", ptr, "sum");

            if (calculateHash(&tempBuf, hashCmd, targetLocation) == -1)
            {
                printf("Hash '%s' error!\n", hashCmd);
                return -1;
            }

            if (doneFirst == 0)
            {
                *buffer = malloc(strlen(tempBuf) + 1);
                sprintf(*buffer, "%s", tempBuf);
                doneFirst++;
            }
            else
            {
                *buffer = realloc(*buffer, strlen(*buffer) + strlen(tempBuf) + 1 + 1);
                sprintf(*buffer, "%s,%s", *buffer, tempBuf);
            }
            free(tempBuf);
        }
        else
        {
            printf("'%s' is not a valid hash function!\n", ptr);
        }

        ptr = strtok(NULL, ",");
    }
    return (doneFirst == 0) ? -1 : 0;
}

int analyseFile(char *hashFunctions, FILE *outputFile, char *targetLocation)
{
    char *fileString = NULL;
    char *statString = NULL;
    char *hashString = NULL;
    char *outputString = NULL;

    if (getFileCmdInfo(&fileString, targetLocation) == -1)
    {
        printf("Error reading file command output!\n");
        return -1;
    }

    if (getStatCmdInfo(&statString, targetLocation) == -1)
    {
        printf("Error reading stat command output!\n");
        return -1;
    }

    if (hashFunctions != NULL)
    {
        if (processHashes(&hashString, hashFunctions, targetLocation) == -1)
        {
            printf("Error calculing hashes!\n");
            return -1;
        }

        outputString = malloc(strlen(targetLocation) + strlen(fileString) + strlen(statString) + strlen(hashString) + 3 + 1);
        sprintf(outputString, "%s,%s,%s,%s", targetLocation, fileString, statString, hashString);
    }
    else
    {
        outputString = malloc(strlen(targetLocation) + strlen(fileString) + strlen(statString) + 2 + 1);
        sprintf(outputString, "%s,%s,%s", targetLocation, fileString, statString);
    }

    if (outputFile)
    {
        fprintf(outputFile, "%s\n", outputString);
    }
    else
    {
        printf("%s\n", outputString);
    }

    free(fileString);
    free(statString);
    free(hashString);
    free(outputString);

    return 0;
}
