#ifndef CMDHELPER_H
#define CMDHELPER_H

int runCmd(char *cmdArgv[], int cmdArgc);
int routeCmd(char *cmdArgv[], int *PIPEREAD_FILENO);
int readRoutedCmdOutput(char **buffer, int PIPEREAD_FILENO);

#endif