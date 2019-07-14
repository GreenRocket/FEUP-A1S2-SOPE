#ifndef FILEANALYSIS_H
#define FILEANALYSIS_H

#include <stdio.h>
#include <time.h>

int checkPathType(const char *path);

int GetFormattedDate(struct tm *ts, char **formattedDate);

int getFileCmdInfo(char **buffer, char *targetLocation);

int getStatCmdInfo(char **buffer, char *targetLocation);

int calculateHash(char **buffer, char *hashCmd, char *targetLocation);

int processHashes(char **buffer, char *hashFunctions, char *targetLocation);

int analyseFile(char *hashFunctions, FILE *outputFile, char *targetLocation);

#endif