/**********************************
 glue layer for fileio and malloc
***********************************/

#define fopen  myport_FileOpen
#define fread  myport_FileRead
#define fclose myport_FileClose
#define fseek  myport_FileSeek
#define ftell  myport_FileTell
#define fprint myport_FilePrintf
#define printf myport_Printf
#define malloc(x) emu_Malloc(x)
//#define malloc(x) myport_malloc(x, __FILE__, __LINE__)
#define free(x) emu_Free(x)
