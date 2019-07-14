#ifndef FLAGS_H
#define FLAGS_H

typedef struct
{
    unsigned int targetIsFolder : 1;
    unsigned int calculateHash : 1;
    unsigned int writeToFile : 1;
    unsigned int logExecution : 1;
} Flags;


#endif