#ifndef ARGVPARSE_H
#define ARGVPARSE_H

#include "flags.h"

int readArguments(int argc, char *argv[], Flags *flags, char **hashFunctions, char **outputFileName, char **targetLocation);

#endif