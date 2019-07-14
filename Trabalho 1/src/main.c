#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> //getcwd
#include "argvParse.h"
#include "fileAnalysis.h"
#include "dirAnalysis.h"
#include "flags.h"

/*
    forensic hello.txt
    forensic -h md5,sha1,sha256 hello.txt
    forensic -r 'folder'
    forensic -h md5 -o output.txt -v hello.txt

    -h [md5, sha1, sha256]  - adicionar sumario criptografico ao output
    -r                      - analisar conteudo do diretorio e subdiretorios
    -o [path/filename]      - gravar para ficheiro o output em vez de stdout
    -v                      - gravar para ficheiro os dados de execução

    Output:
        file_name,file_type,file_size,file_access,file_created_date,file_modification_date,md5,sha1,sha256

    Lidar com ^c - SIGINT
    Se flag -o for ativada, usar SIGUSR1/SIGUSR2 para imprimir info de dir/file à medida que são encontrados.
    New directory: n/m directories/files at this time.
*/

//char *outputPath = NULL;
// outputPath = getcwd(NULL, 0);
// outputPath = malloc(strlen(outputPath) + strlen(outputFileName) + 1 + 1);
// sprintf(outputPath, "%s/%s", outputPath, outputFileName);

//TODO: Fazer write para pipe/temp file em vez de usar estas strings todas
//TODO: Verificar free() e malloc()
//TODO: Rever exit branches + msgs
//TODO: log
//TOOD: handle ctrl+c
//TODO: feedback em "-o", SIGUSR1/SIGUSR2

/* TODO: ESTADO DOS COMENTARIOS
        main ok!
        argvParse ok!

*/

int main(int argc, char *argv[]) //char *envp[]
{
    // Declarar variaveis
    Flags flags = {0, 0, 0, 0};
    char *targetLocation = NULL;
    char *hashFunctions = NULL;
    char *outputFileName = NULL;
    FILE *outputFile = NULL;

    // Ler e processar argumentos do programa
    if (readArguments(argc, argv, &flags, &hashFunctions, &outputFileName, &targetLocation) != 0)
        return -1;

    // Se a flag de escrito para ficheiro estiver activada, tentar abrir o ficheiro indicado nos argumentos.
    if (flags.writeToFile)
    {
        // Abrir em mode append "a", para escrever sempre no fim do documento.
        // Ficheiro é criado caso não exista.
        outputFile = fopen(outputFileName, "a");
        if (outputFile == NULL)
            exit(EXIT_FAILURE);
    }

    // Analisar conteudo do diretório e subdiretórios recursivamente.
    if (flags.targetIsFolder)
    {
        if (analyseDir(argv, argc, hashFunctions, outputFile, targetLocation))
        {
            printf("Failed to analyse directory '%s'\n", targetLocation);
            return -1;
        }
    }
    // Analisar apenas ficheiro/diretório
    else
    {
        if (analyseFile(hashFunctions, outputFile, targetLocation) == -1)
        {
            printf("Failed to analyse file '%s'\n", targetLocation);
            return -1;
        }
    }

    // Limpeza
    if (outputFile)
        fclose(outputFile);
    if (outputFileName)
        free(outputFileName);
    if (hashFunctions)
        free(hashFunctions);
    if (targetLocation)
        free(targetLocation);

    return 0;
}
