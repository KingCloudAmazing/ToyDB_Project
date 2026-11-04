#ifndef TOYDB_PF_API_H
#define TOYDB_PF_API_H

/*
 * Typed declarations for the legacy ToyDB PF interface.
 * The original pflayer/pf.h exposes only PF_Init/PF_PrintError and error
 * constants; these declarations match the definitions in pflayer/pf.c.
 */

#include "../pflayer/pf.h"

#ifdef __cplusplus
extern "C" {
#endif

extern int PF_CreateFile(char *fname);
extern int PF_DestroyFile(char *fname);
extern int PF_OpenFile(char *fname);
extern int PF_CloseFile(int fd);
extern int PF_GetFirstPage(int fd, int *pagenum, char **pagebuf);
extern int PF_GetNextPage(int fd, int *pagenum, char **pagebuf);
extern int PF_GetThisPage(int fd, int pagenum, char **pagebuf);
extern int PF_AllocPage(int fd, int *pagenum, char **pagebuf);
extern int PF_DisposePage(int fd, int pagenum);
extern int PF_UnfixPage(int fd, int pagenum, int dirty);

#ifdef __cplusplus
}
#endif

#endif /* TOYDB_PF_API_H */
