/**************************************************************************************************/
/* CMSRUNTM.H - CMS Runtime Logic header                                                          */
/*                                                                                                */
/* Part of GCCLIB - VM/370 CMS Native Std C Library; A Historic Computing Toy                     */
/*                                                                                                */
/* Contributors: See contrib memo                                                                 */
/*                                                                                                */
/* Released to the public domain.                                                                 */
/**************************************************************************************************/

#ifndef CMSRUNTM_INCLUDED
#define CMSRUNTM_INCLUDED
#include <stdio.h>

#ifndef __SIZE_T_DEFINED
#define __SIZE_T_DEFINED
typedef unsigned long size_t;
#endif

/* mspace is an opaque type representing an independent
   region of space that supports malloc(), etc. */
typedef void* mspace;

/* Function Pointers */
typedef int (MAINFUNC)(int argc, char *argv[]); /* declare a main() function pointer */

/* The CMSCRAB macro maps the GCC stack. */
typedef struct CMSCRAB CMSCRAB;

/**************************************************************************************************/
/* M U S T   B E   S Y N C E D   W I T H   C M S C R A B   M A C R O                              */
/**************************************************************************************************/
struct CMSCRAB {
  void *dstack;                                       /* Dynamic Stack Control (see below) +00 */
  CMSCRAB *backchain;                                  /*  backchain to previous save area +04 */
  CMSCRAB *forward;                                     /* forward chain to next save area +08 */
  void *regsavearea[15];                      /* register save area and save area chaining +12 */
  GCCCRAB *gcccrab;                               /* GCC C Runtime Anchor Block (GCCCRAB)  +72 */
  void *stackNext;                                     /* next available byte in the stack +76 */
  void *numconv;                                              /* numeric conversion buffer +80 */
  void *funcrslt;                                                /* function result buffer +84 */
  unsigned char locals[];                                           /* Local Variables etc +88 */
};

/* To get the addresses of the CMS crab */
#define GETCMSCRAB() ({CMSCRAB *theCRAB; __asm__("LR %0,13" : "=d" (theCRAB)); theCRAB;})

/**************************************************************************************************/
/* Dynamic Stack Control                                                                          */
/* Low 24 bits contain the address of the byte past the current stack bin                         */
/* The high byte contains the control flags (bits):                                               */
/*  0 - Stack control not in use - ignore (for backward compatability)                            */
/*  1 - Static stack - do not extend with new bins                                                */
/*  2 - Dynamic Stack - can extend                                                                */
/*  4 - I am the first stackframe in the bin (needed to for popping logic to dealocate bins)      */
/*      Note 4 is used with 2 e.g. 6 ...                                                          */
/**************************************************************************************************/
/* Min bin size */
#define _DSK_MINBIN  16*1024
/* Flags */
#define _DSK_NOTUSED 0
#define _DSK_STATIC  1
#define _DSK_DYNAMIC 2
#define _DSK_FIRST   4
#define _DSK_FIRSTDYNAMIC 6
/**************************************************************************************************/

/* Call parameters */
#define MAXEPLISTARGS 32
#define MAXPLISTARGS 16
#define TRUNCPLISTARGS MAXPLISTARGS-8 /* Truncated PLIST: 8 = fence + marker + 6 block eplist */
#define ARGBUFFERLEN 300

typedef char PLIST[8]; /* 8 char block */

typedef struct EVALBLOK EVALBLOK;

struct EVALBLOK {
  EVALBLOK *Next;  /* Reserved - but obvious what the intention was! */
  int BlokSize;    /* Total block size in DW's */
  int Len;         /* length of data in bytes */
  int Pad;         /* (reserved) */
  char Data[];     /* the data... */
};

typedef struct ADLEN {
  char *Data;    /* data... */
  int Len;       /* length of data in bytes */
} ADLEN;

typedef struct EPLIST {
  char *Command;
  char *BeginArgs;          /* start of Argstring */
  char *EndArgs;            /* character after end of the Argstring */
  void *CallContext;        /* Extention Point - IBM points it to a FBLOCK */
  ADLEN *ArgLlist;          /* FUNCTION ARGUMENT LIST - Calltype 5 only */
  EVALBLOK *FunctionReturn; /* RETURN OF FUNCTION - Calltype 5 only */
} EPLIST;

#define EPLISTMARKER "*EPLIST" /* Includes the terminating null */

/* The structure used by GCCLIB designed so that we can find the EPLIST
   when R0 is corrupted */
typedef struct GCCLIBPLIST {
  PLIST plist[TRUNCPLISTARGS + 1];
  char marker[8];
  EPLIST eplist;
} GCCLIBPLIST;

/* Startup Functions */
int __cstub(PLIST *plist , EPLIST *eplist);
int __cstart(MAINFUNC* mainfunc, PLIST *plist , EPLIST *eplist);

/*
  creat_msp creates, updates GCCCRAB, and returns a new independent space
  with the given initial capacity of the default granularity size
  (16kb).  It returns null if there is no system memory available to
  create the space. Large Chunk Trcking is Enabled.
*/
mspace creat_msp();

/*
  dest_msp destroys the memory space (from GCCCRAB), and attempts
  to return all of its memory back to the system, returning the total number
  of bytes freed. After destruction, the results of access to all memory
  used by the space become undefined.
*/
size_t dest_msp();

/*
  Creates a new stack bin and adds and returns the first frame to the bin.
  The returned frame is of the requested size and is configured and chained to
  the existing frame
  Called by the dynst.assemble routines when the current stack bin runs out of
  space.
*/
CMSCRAB* morestak(CMSCRAB* frame, size_t requested);

/*
  Removes/cleans up unused stack bins.
  Called by the dynst.assemble routines when a first frame in the bin is
  popped / exited.
*/
void lessstak(CMSCRAB* frame);

/**************************************************************************************************/
/* IO Structures / function / defines                                                             */
/**************************************************************************************************/
/**************************************************************************************************/
/* This library provides the following capbilities:                                               */
/*   - For variable length disk files:                                                            */
/*       Can be opened for r=reading, w=writing (truncate), or a=appending                        */
/*       b=binary which causes \n to NOT act as record seperators                                 */
/*       setting / getting position is not supported                                              */
/*       +=reading/writing is not supported                                                       */
/*       caching is not supported (and irrelevent)                                                */
/*   - For fixed length disk files:                                                               */
/*       Can be opened for r=reading, w=writing (truncate), a=appending,                          */
/*       r+=read/write, w+=read/write (truncate on open), or a+ (read and append writes)          */
/*       b=binary which causes \n to NOT act as record seperators                                 */
/*       setting / getting position is supported                                                  */
/*       caching is supported                                                                     */
/*       Because CMS does not support read/write files this is emulated by the library            */
/*       In summary: random acsess files are suppored if and only if recfm=F                      */
/*    - Non-disk file streams (Console, reader, punch, printer)                                   */
/*       Can be opened for r=reading, w=writing or a=appending (treated as writing) if appropriate*/
/*       b=binary which causes \n to NOT act as record seperators                                 */
/*       setting / getting position is not supported                                              */
/*       +=reading/writing is not supported                                                       */
/*       caching is not supported (or relavent)                                                   */
/**************************************************************************************************/
typedef struct CMSDRIVER CMSDRIVER;
typedef struct CMSDRIVERS CMSDRIVERS;
typedef struct CMSFILECACHE CMSFILECACHE;
typedef struct CMSCACHEENTRY CMSCACHEENTRY;

struct FILE {
   char validator1;                                               /* Marks a valid FILE structure */
   char name[21];                                                  /* File name used for messages */
   char fileid[19];                                                     /* Null terminated FILEID */
   FILE* next;                                                      /* Next file in the file list */
   FILE* prev;                                                  /* Previous file in the file list */
   int access;                                   /* type of access mode flags (read, write, etc.) */
   int status;           /* status flags (error, eof, dirty record buffer, read/write mode, etc.) */
   int error;                             /* error code from last I/O operation against this file */
   int ungetchar;                                                              /* Unget Character */
   int recpos;   /* char position in record buffer, next unread byte, next byte position to write */
   int recnum;   /* Record number (1 base) of the record in the buffer, -1 nonblock device,
                                                                         0 no record loaded */
   int reclen;                        /* Current Record length excluding any trailing \n and null */
   int maxreclen;       /* Max Record length for curren record excluding any trailing \n and null */
   int filemaxreclen;     /* Max Record length / Buffer Length excluding any trailing \n and null */
   int records;                                     /* Number of records or -1 for non-block file */
   CMSDRIVER *device;                                      /* device driver (console, disk, etc.) */
   char *buffer;                                                                 /* record buffer */
   CMSFILECACHE *cache;                                                             /* File cache */
   CMSFILE fscb;                      /* the CMS File System Control Block (if it is a disk file) */
   char validator2;                                               /* Marks a valid FILE structure */
};

/* Status flags */
#define STATUS_EOF         1
#define STATUS_DIRTY       2
#define STATUS_READ        4                           /* File is currently in read mode */
#define STATUS_WRITE       8                          /* File is currently in write mode */
#define STATUS_CACHE       16                                /* File can support a cache */

/* Access flags */
#define ACCESS_TEXT        1
#define ACCESS_READ        2
#define ACCESS_WRITE       4
#define ACCESS_TRUNCATE    8
#define ACCESS_APPEND      16
#define ACCESS_READWRITE   32
#define ACCESS_TEMPFILE    64

/* Cache */
#define FCACHEBUCKETS 97                                                          /* Prime number */
struct CMSFILECACHE {
  size_t provided_cache_size;             /* Provided cache size - or 0 if we malloced the buffer */
  int noentries;                                          /* Number of Entries available in cache */
  size_t entrysize;                                                         /* size of each entry */
  long hits;                                                  /* Number of successfull cache hits */
  long misses;                                                          /* Number of cache misses */
  CMSCACHEENTRY* bucket[FCACHEBUCKETS];                                           /* Bucket Lists */
  CMSCACHEENTRY* newestused;                                /* The most recently used cache entry */
  CMSCACHEENTRY* oldestused;      /* The entry that was used last - i.e. the candidate to replace */
  /* Entries start here */
};

struct CMSCACHEENTRY {
  size_t recnum;                                                   /* file record number (1 based) */
  int reclen;                                                              /* Cached Record Length */
  int maxreclen;                                                       /* Cached Max Record Length */
  CMSCACHEENTRY* nextinbucket;                                        /* next entry in this bucket */
  CMSCACHEENTRY* previnbucket;                                    /* previous entry in this bucket */
  CMSCACHEENTRY* nextlastused;    /* next entry in the newest used chain (one used more recentyly) */
  CMSCACHEENTRY* prevlastused;    /* previous entry in the newest used chain (used less recentyly) */
  /* Cached data starts here */
};

/* IO Drivers */
typedef int (CONTROL_FUNC)(FILE *stream);
typedef int (OPEN_FUNC)(char filespecwords[][10], FILE* file);
typedef int (SETPOS_FUNC)(FILE *stream, int recpos);

struct CMSDRIVER {
  OPEN_FUNC *open_func;
  CONTROL_FUNC *close_func;
  CONTROL_FUNC *getpos_func;
  CONTROL_FUNC *getend_func;
  SETPOS_FUNC *setpos_func;
  CONTROL_FUNC *write_func;
  CONTROL_FUNC *read_func;
  CONTROL_FUNC *postread_func;
};

struct CMSDRIVERS {
  char *name;
  CMSDRIVER *driver;
};

/* IO internal functions */
void _clfiles(); /* Close all files - for exit routine */
int _isopen(char* fileid); /* See if a file is open */

/**************************************************************************************************/

#endif
