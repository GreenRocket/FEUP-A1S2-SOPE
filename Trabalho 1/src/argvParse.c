#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "argvParse.h"

int readArguments(int argc, char *argv[], Flags *flags, char **hashFunctions, char **outputFileName, char **targetLocation)
{
    // Percorrer todos os argumentos, saltando o primeiro (nome do programa).
    for (int i = 1; i < argc; i++)
    {
        // Se encontrarmos a flag "-r", marcá-la
        if (strcmp(argv[i], "-r") == 0)
            flags->targetIsFolder = 1;

        // Se encontrarmos a flag "-v", marcá-la
        else if (strcmp(argv[i], "-v") == 0)
            flags->logExecution = 1;

        // Se encontrarmos a flag "-h":
        else if (strcmp(argv[i], "-h") == 0)
        {
            // Verificar se existe um argumento seguinte:
            i++;
            if (i < argc)
            {
                // Se existir, marcar a flag e guardar o tipo de hashes a calcular.
                flags->calculateHash = 1;
                if ((*hashFunctions = malloc(strlen(argv[i] + 1))) == NULL)
                    return -1;
                strcpy(*hashFunctions, argv[i]);
            }
            else
            {
                // Se não existir, terminar execução.
                printf("Tipo de hash após \"-h\" em falta!\n");
                return -1;
            }
        }

        // Se encontrarmos a flag "-o":
        else if (strcmp(argv[i], "-o") == 0)
        {
            // Verificar se existe um argumento seguinte:
            i++;
            if (i < argc)
            {
                // Se existir, marcar a flag e guardar o nome do ficheiro onde escrever.
                flags->writeToFile = 1;
                if ((*outputFileName = malloc(strlen(argv[i] + 1))) == NULL)
                    return -1;
                strcpy(*outputFileName, argv[i]);
            }
            else
            {
                // Se não existir, terminar execução.
                printf("Ficheiro de output após \"-o\" em falta!\n");
                return -1;
            }
        }

        // Se o argumento actual não corresponder a nenhuma flag, assumir que é o ficheiro/diretório a analisar.
        else
        {
            size_t length = strlen(argv[i]) + 1;
            *targetLocation = malloc(length);
            memcpy(*targetLocation, argv[i], length);
        }
    }

    // Se após ler os argumentos, o alvo a analisar continuar vazio, terminar execução.
    if (*targetLocation == NULL)
    {
        printf("Ficheiro a analisar em falta!\n");
        return -1;
    }

    return 0;
}