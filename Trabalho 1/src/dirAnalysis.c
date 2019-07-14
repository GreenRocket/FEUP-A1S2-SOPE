#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "fileAnalysis.h"
#include "cmdHelper.h"
#include "dirAnalysis.h"

int analyseDir(char *argv[], int argc, char *hashFunctions, FILE *outputFile, char *targetLocation)
{
    DIR *dir;
    if ((dir = opendir(targetLocation)) == NULL)
    {
        perror("Opendir() error");
        return -1;
    }

    struct dirent *dent;
    while ((dent = readdir(dir)) != NULL)
    {
        if (strcmp(dent->d_name, ".") != 0 && strcmp(dent->d_name, "..") != 0) //Ignorar paths que não estão dentro da folder
        {
            char *path;
            path = malloc(strlen(targetLocation) + strlen(dent->d_name) + 1 + 1);
            sprintf(path, "%s/%s", targetLocation, dent->d_name);

            int pathType = checkPathType(path);
            if (pathType == 0) // Ser ficheiro
            {
                // char *outputString = NULL;
                if (analyseFile(hashFunctions, outputFile, path) == -1) // Analisar ficheiro em questão
                {
                    printf("Failed to analyse file '%s'\n", path);
                    continue;
                }

                // if (outputFile != NULL)
                // {
                //     fprintf(outputFile, "%s\n", outputString);
                // }
                // else
                // {
                //     printf("%s\n", outputString);
                // }
                // free(outputString);
            }
            else if (pathType == 1) // Ser Diretório
            {
                size_t length = strlen(path) + 1;
                argv[argc - 1] = malloc(length);
                memcpy(argv[argc - 1], path, length);
                if (runCmd(argv, argc) != 0)
                {
                    printf("Error running file command!\n");
                    return -1;
                }
            }
            else // Erro na análise do tipo do Path
            {
                printf("Erro!\n");
            }
        }
    }
    closedir(dir);

    return 0;
}