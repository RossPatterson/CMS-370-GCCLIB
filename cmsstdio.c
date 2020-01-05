/**************************************************************************************************/
/* CMSSTDIO.C: Native CMS implementation of STDIO.H.                                              */
/*                                                                                                */
/* Not implemented:                                                                               */
/*     int fgetpos(FILE * stream, fpos_t * position)                                              */
/*     int fseek(FILE * stream, long offset, int origin)                                          */
/*     int fsetpos(FILE * stream, const fpos_t * position)                                        */
/*     void perror(const char * str)                                                              */
/*     void rewind(FILE * stream)                                                                 */
/*     int snprintf(char * buffer, int buff_size, const char * format, ...)                       */
/*                                                                                                */
/* Note also that we depend on the offset of the GCC CRAB pointer in the save area, currently 72  */
/* (decimal) bytes.  If the CMSCRAB macro is changed, this code must change!!!!!!!!!!!!!!!!!!!!!  */
/*                                                                                                */
/* Robert O'Hara, Redmond Washington, May 2009                                                    */
/*                                                                                                */
/* Based on code written by Paul Edwards and Dave Wade.                                           */
/* Released to the public domain.                                                                 */
/**************************************************************************************************/
#include <stdio.h>
#include <ctype.h>
#include <float.h>
#include <time.h>

/* Fix the CMSconsoleWrites with one arg in this file - got bored fixing each one - Adrian */
#define ConsoleWrite(s1) (__wrterm((s1),0))


// The CMSCRAB macro maps the GCC stack.  In the first stack frame are pointers to useful global
// variables used by routines in CMSSTDIO.  Eventually I'll fill in more of the structure below.
typedef struct {                                                   // map the start of the GCC stack
   void * savearea[18];                             // register save area and save area chaining +00
   void * crab;                              // pointer to the GCC C Runtime Anchor Block (CRAB) +72
   void * stackNext;                                         // next available byte in the stack +76
   void * numconv;                                                  // numeric conversion buffer +80
   void * funcrslt;                                                    // function result buffer +84
   FILE * consoleOutputFile;                      // address of FILE structure for console ouput +88
   FILE * consoleInputFile;                       // address of FILE structure for console input +92
   FILE actualConsoleOutputFileHandle;
   FILE actualConsoleInputFileHandle;
   char consoleInputBuffer[131];
   } CMSCRAB;                                                          // CMS C Runtime Anchor Block
#define LOADCRAB "L %0,72(13)"

static void dblcvt(double num, char cnvtype, size_t nwidth, size_t nprecision, char *result);
static int examine(const char **formt, FILE *fq, char *s, va_list *arg, int chcount);
int GetFileid(const char * fname, char * fileid);
static int vvprintf(const char *format, va_list arg, FILE *fq, char *s);
static int vvscanf(const char * format, va_list arg, FILE *fp, const char * s);

// The following defines are used in vvprintf and vvscanf.
#define unused(x) ((void)(x))
#define outch(ch) ((fq == NULL) ? *s++ = (char)ch : putc(ch, fq))
#define inch() ((fp == NULL) ? (ch = (unsigned char)*s++) : (ch = getc(fp)))

// In CMS, these are the only 6 relevant file access modes.  Bit 1 (ACCESS_WRITING) is on for
// writing, off for reading.  Bit 2 (ACCESS_TEXT) is on for text files, off for binary files, and
// bits 3 and 1 (ACCESS_APPENDING) are on for appending.
#define ACCESS_WRITING    1
#define ACCESS_TEXT       2
#define ACCESS_APPENDING  5
#define ACCESS_READ_BIN   0                                                                  // 0000
#define ACCESS_WRITE_BIN  1                                                                  // 0001
#define ACCESS_READ_TXT   2                                                                  // 0010
#define ACCESS_WRITE_TXT  3                                                                  // 0011
#define ACCESS_APPEND_BIN 5                                                                  // 0101
#define ACCESS_APPEND_TXT 7                                                                  // 0111
#define NUM_MODES 15

// Note that we treate read/write (+) mode as read.
static const char modes[NUM_MODES][4] =
   {"r", "r+", "w", "w+", "a", "a+", "rb", "rb+", "r+b", "wb", "wb+", "w+b", "ab", "ab+", "a+b"};
static const int modeVals[NUM_MODES] = {
   ACCESS_READ_TXT, ACCESS_READ_TXT, ACCESS_WRITE_TXT, ACCESS_WRITE_TXT, ACCESS_APPEND_TXT,
   ACCESS_APPEND_TXT, ACCESS_READ_BIN, ACCESS_READ_BIN, ACCESS_READ_BIN, ACCESS_WRITE_BIN,
   ACCESS_WRITE_BIN, ACCESS_WRITE_BIN, ACCESS_APPEND_BIN, ACCESS_APPEND_BIN, ACCESS_APPEND_BIN};

// Here are the specific devices for which a stream may be opened.
#define NUM_DEVICES 5
#define DEVICE_CON  0
#define DEVICE_DSK  1
#define DEVICE_PRT  2
#define DEVICE_PUN  3
#define DEVICE_RDR  4


void clearerr(FILE * stream)
/**************************************************************************************************/
/* void clearerr(FILE * stream)                                                                   */
/*                                                                                                */
/* Resets the error status of the specified stream to 0.                                          */
/*    stream   a pointer to the open input stream.                                                */
/**************************************************************************************************/
{
if (stream == NULL) {ConsoleWrite("clearerr error: stream is NULL.\n"); return EOF;}
stream->error = 0;
}                                                                                 // end of clearerr


int fclose(FILE * stream)
/**************************************************************************************************/
/* int fclose(FILE * stream)                                                                      */
/*                                                                                                */
/* Close the open stream in an appropriate and orderly fashion.                                   */
/*    stream    is a pointer to the open output stream.                                           */
/*                                                                                                */
/* Returns:                                                                                       */
/*    0 if all is well, EOF if there is an error.                                                 */
/**************************************************************************************************/
{
char pad;

if (stream == NULL) {ConsoleWrite("fclose error: stream is NULL.\n"); return EOF;}
switch (stream->device) {
   case DEVICE_CON:
      return 0;                                                // nothing to do for a console stream
   case DEVICE_DSK:
      if ((stream->access & ACCESS_WRITING) && (stream->next > stream->buffer)) {   // data to write
         if ((stream->fscb.format[0] == 'F') && (stream->next < stream->last)) {       // pad buffer
            if (stream->access & ACCESS_TEXT) pad = ' '; else pad = 0;          // set pad character
            for (; stream->next < stream->last; stream->next++) stream->next[0] = pad;
            }
         stream->error = CMSfileWrite(&stream->fscb, -1, stream->next - stream->buffer);
         if (stream->error != 0) {
            printf("fclose error: return code %u from FSWRITE.\n", stream->error);    // remove this
            return EOF;
            }
         }
      CMSfileClose(&stream->fscb);
      break;
   case DEVICE_PRT:
      if ((stream->access & ACCESS_WRITING) && (stream->next > stream->buffer)) {   // data to write
         stream->next = 0;                                                   // terminate the string
         stream->error = CMSprintLine(stream->buffer);                                   // print it
         if (stream->error != 0) {
            printf("fclose error: return code %u from PRINTL.\n", stream->error);     // remove this
            return EOF;
            }
         }
      CMScommand("CP CLOSE PRINTER", CMS_COMMAND);                              // close the printer
      break;
   case DEVICE_PUN:
      if ((stream->access & ACCESS_WRITING) && (stream->next > stream->buffer)) {   // data to write
         for (; stream->next < stream->last; stream->next++) stream->next[0] = ' ';  // pad the card
         stream->error = CMScardPunch(stream->buffer);                                   // punch it
         if (stream->error != 0) {
            printf("fclose error: return code %u from PUNCHC.\n", stream->error);     // remove this
            return EOF;
            }
         }
      CMScommand("CP CLOSE PUNCH", CMS_COMMAND);                             // close the card punch
      break;
   case DEVICE_RDR:
      CMScommand("CP CLOSE READER", CMS_COMMAND);                           // close the card reader
      break;
   }
if (CMSmemoryFree(stream) == 0) return 0;
else return EOF;
}                                                                                   // end of fclose

int feof(FILE * stream)
/**************************************************************************************************/
/* int feof(FILE * stream)                                                                        */
/*                                                                                                */
/* Report whether end of file has been reached for the specified input stream.                    */
/*    stream   a pointer to the open input stream.                                                */
/*                                                                                                */
/* Returns:                                                                                       */
/*    1 if EOF has been reached, 0 otherwise.                                                     */
/**************************************************************************************************/
{
if (stream == NULL) {ConsoleWrite("feof error: stream is NULL.\n"); return EOF;}
return stream->eof;
}                                                                                     // end of feof


int ferror(FILE * stream)
/**************************************************************************************************/
/* int ferror(FILE * stream)                                                                      */
/*                                                                                                */
/* Returns the error status of the specified stream.                                              */
/*    stream   a pointer to the open input stream.                                                */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the error status, or 0 if there is no error.                                                */
/**************************************************************************************************/
{
if (stream == NULL) {ConsoleWrite("ferror error: stream is NULL.\n"); return EOF;}
return stream->error;
}                                                                                   // end of ferror


int fflush(FILE * stream)
/**************************************************************************************************/
/* int fflush(FILE * stream)                                                                      */
/*                                                                                                */
/* This function does nothing.                                                                    */
/*                                                                                                */
/* Returns:                                                                                       */
/*    0, indicating that all is well.                                                             */
/**************************************************************************************************/
{
return 0;
}                                                                                   // end of fflush


int fgetc(FILE * stream)
/**************************************************************************************************/
/* int fgetc(FILE * stream)                                                                       */
/*                                                                                                */
/* Read the next character from the specified output stream.                                      */
/*    stream   a pointer to the open output stream.                                               */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the character, or EOF if there is an error.                                                 */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  A newline character is added at the end of each physical line read when reading TEXT    */
/*        files.                                                                                  */
/**************************************************************************************************/
{
char c;
char errmsg[80];
int num;

if (stream == NULL) {ConsoleWrite("fgetc error: stream is NULL.\n"); return EOF;}
switch (stream->device) {
   case DEVICE_CON:
      if (stream->access & ACCESS_READ_TXT) {                              // open for reading text?
         if (stream->ungetChar >= 0) {               // was a character pushed back onto the stream?
            c = stream->ungetChar;                                                   // yes, read it
            stream->ungetChar = -1;                                 // set the unget buffer to empty
            break;
            }
         if (stream->next == stream->last) {                        // empty buffer, read a new line
            num = CMSconsoleRead(stream->buffer);
            stream->next = stream->buffer;
            stream->buffer[num] = '\n';          // add a newline character at the end of the buffer
            stream->last = stream->buffer + num + 1;         // point to first byte after bytes read
            }
         c = stream->next[0];                                               // deliver the character
         stream->next++;
         }
      else {
         stream->error = 9;
         ConsoleWrite("fgetc error: file not open for text input.\n");  // remove this
         return EOF;
         }
      break;
   case DEVICE_DSK:
      if (stream->access & ACCESS_WRITING) {                                    // open for writing?
         stream->error = 9;
         ConsoleWrite("fgetc error: file not open for input.\n");       // remove this
         return EOF;
         }
      else {
         if (stream->ungetChar >= 0) {               // was a character pushed back onto the stream?
            c = stream->ungetChar;                                                   // yes, read it
            stream->ungetChar = -1;                                 // set the unget buffer to empty
            break;
            }
         if (stream->next == stream->last) {                        // empty buffer, read a new line
            stream->error = CMSfileRead(&stream->fscb, 0, &num);
            switch (stream->error) {
               case 0:                                                                // no problems
                  stream->next = stream->buffer;
                  if (stream->access & ACCESS_TEXT) stream->buffer[num++] = '\n';       // append NL
                  stream->last = stream->buffer + num;       // point to first byte after bytes read
                  break;
               case 12:                                       // we have reached the end of the file
                  stream->error = 0;                                                 // not an error
                  stream->eof = 1;                                                  // remember this
                  return EOF;
                  break;
               default:                                               // error reading from the file
                  sprintf(errmsg, "fgetc error: return code %d from CMSfileRead.\n", stream->error);
                  ConsoleWrite(errmsg);                                 // remove this
                  return EOF;
                  break;
               }
            }
         c = stream->next[0];                                               // deliver the character
         stream->next++;
         }
      break;
   case DEVICE_PRT:
      stream->error = 9;
      ConsoleWrite("fgetc error: cannot read from the printer.\n");    // remove this
      return EOF;
      break;
   case DEVICE_PUN:
      stream->error = 9;
      ConsoleWrite("fgetc error: cannot read from the card punch.\n");  // remove this
      return EOF;
      break;
   case DEVICE_RDR:
      if (stream->access & ACCESS_WRITING) {                                    // open for writing?
         stream->error = 9;
         ConsoleWrite("fgetc error: file not open for input.\n");       // remove this
         return EOF;
         }
      else {
         if (stream->ungetChar >= 0) {               // was a character pushed back onto the stream?
            c = stream->ungetChar;                                                   // yes, read it
            stream->ungetChar = -1;                                 // set the unget buffer to empty
            break;
            }
         if (stream->next == stream->last) {                        // empty buffer, read a new line
            stream->error = CMScardRead(stream->buffer, &num);   // should we strip trailing blanks?
            stream->next = stream->buffer;
            stream->last = stream->buffer + num;             // point to first byte after bytes read
            }
         c = stream->next[0];                                               // deliver the character
         stream->next++;
         }
      break;
   }
return c;
}                                                                                    // end of fgetc


char * fgets(char * str, int num, FILE * stream)
/**************************************************************************************************/
/* char * fgets(char * str, int num, FILE * stream)                                               */
/*                                                                                                */
/* Read up to 'num' - 1 characters from the specified file stream and place them into 'str',      */
/* terminating them with a NULL.  fgets() stops when it reaches the end of a line, in which case  */
/* 'str' will contain a newline character.  Otherwise, fgets() will stop when it reaches 'num'-1  */
/* characters or encounters the EOF character.                                                    */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the a pointer to 'str', or NULL on EOF or if there is an error.                             */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  fgets is only appropriate for a text file.                                              */
/**************************************************************************************************/
{
int c;
int i;

if (stream == NULL) {
   ConsoleWrite("fgets error: stream is NULL.\n");
   return (char *) EOF;
}
for (i = 0; i < num - 1; i++) {
   c = fgetc(stream);                             // some day I will fix this to avoid calling fgetc
   if (c == EOF) {                                                     // end of file or other error
      if (i > 0) {c = 0; break;}             // return what we have read (next call will return EOF)
      return NULL;
      }
   str[i] = c;
   if (c == '\n') {str[i + 1] = 0; break;}        // newline means we are done: terminate the string
   }
return str;
}                                                                                    // end of fgets


FILE * fopen(const char * filespec, const char * access)
/**************************************************************************************************/
/* FILE * fopen(const char * filespec, const char * access)                                       */
/*                                                                                                */
/* Open the specified file and return a stream associated with that file.                         */
/*    filespec is a pointer to a string containing the specification of the file to be opened:    */
/*             "CON:"   terminal console (read or write)                                          */
/*             "PRT:"   printer (write only)                                                      */
/*             "PUN:"   card punch (write only)                                                   */
/*             "RDR:"   card reader (read only)                                                   */
/*             "DSK:    filename filetype filemode [F|V [recordLength]]"                          */
/*                      disk file (read or write), where:                                         */
/*                      filename is the up to 8 character name.                                   */
/*                      filetype is the up to 8 character type.                                   */
/*                      filemode is the up to 2 character disk mode leter and optional number.    */
/*                      F|V      specifies the record format fixed or variable.  It is ignored    */
/*                               when opening a file for reading.                                 */
/*                      reclen   specifies the record length.  It is required for fixed-length    */
/*                               files, and is taken as the maximum record length for variable-   */
/*                               length files.  It is ignored when opening a file for reading     */
/*                                                                                                */
/*    access   specifies how the file will be accessed (i.e. for input, output, etc):             */
/*             "r"    Open a text file for reading                                                */
/*             "w"    Create a text file for writing                                              */
/*             "a"    Append to a text file                                                       */
/*             "r+"   Open a text file for read/write                                             */
/*             "w+"   Create a text file for read/write                                           */
/*             "a+"   Open a text file for read/write                                             */
/*             "rb"   Open a binary file for reading                                              */
/*             "wb"   Create a binary file for writing                                            */
/*             "ab"   Append to a binary file                                                     */
/*             "rb+"  Open a binary file for read/write                                           */
/*             "wb+"  Create a binary file for read/write                                         */
/*             "ab+"  Open a binary file for read/write                                           */
/*             "r+b"  Open a binary file for read/write                                           */
/*             "w+b"  Create a binary file for read/write                                         */
/*             "a+b"  Open a binary file for read/write                                           */
/*                                                                                                */
/* Returns:                                                                                       */
/*    a pointer to the stream associated with the open file, or NULL on failure.                  */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  The maximum record length is 65535 bytes.                                               */
/*    2.  The maximum number of records in a file is 32768.                                       */
/*    3.  The maximum number of 800-byte blocks in a file is 16060.                               */
/*    4.  When writing a text file, a newline character signals the end of the current record.    */
/*    5.  When reading a text file, a newline character is inserted at the end of each record.    */
/*    6.  When writing a fixed-length file, the buffer will be padded with blanks if needed.      */
/**************************************************************************************************/
{
char * oops;
char * s;
char devices[NUM_DEVICES][5] = {"CON:", "DSK:", "PRT:", "PUN:", "RDR:"};
char errmsg[80];
char fileid[19];
char fileinfo[40];
char filename[19];
char recfm;
CMSCRAB * theCRAB;
FILE * theFile;
int i;
int accessMode;
int devtype;
int lrecl;
int rc;

__asm__("L %0,72(13)" : "=d" (theCRAB));                                      // get address of CRAB

// First check the access mode argument.
for (i = 0; i < NUM_MODES; i++) if (strcmp(access, modes[i]) == 0) break;
if (i == NUM_MODES) {                                                                // invalid mode
   sprintf(errmsg, "fopen error: invalid access mode '%s' specified.\n", access);
   ConsoleWrite(errmsg);// should really use PERROR for all of these error messages!!!
   return NULL;
   }
accessMode = modeVals[i];                                                // remember the access mode

// Now check the file specification.
strncpy(fileinfo, filespec, sizeof(fileinfo));             // because strtok modifies the string :-(
s = strtok(fileinfo, " ");
for (i = 0; i < NUM_DEVICES; i++) if (strcmp(s, devices[i]) == 0) break;
if (i == NUM_DEVICES) {
   sprintf(errmsg, "fopen error: invalid device '%s' specified.\n", s);
   ConsoleWrite(errmsg);
   return NULL;
   }
else devtype = i;                                                        // remember the device type

// For a file, we must check the record format and lrecl parameters as well.
if (devtype == DEVICE_DSK) {                          // examine the rest of the disk file arguments
   recfm = 'V';                                                             // default record format

   // we should open the file, get the lrecl, then allocate the buffer!!!!!!!!

   lrecl = 65535;                                      // assume the worst for variable format files
   memset(fileid, ' ', sizeof(fileid));                               // initialize fileid to blanks
   s = strtok(NULL, " ");                                                            // get filename
   if (s == NULL || strlen(s) > 8) {
      sprintf(errmsg, "fopen error: missing/invalid filename '%s' specified.\n", s);
      ConsoleWrite(errmsg);
      return NULL;
      }
   else {memcpy(fileid, s, strlen(s)); strcpy(filename, s); strcat(filename, " ");}
   s = strtok(NULL, " ");                                                            // get filetype
   if (s == NULL || strlen(s) > 8) {
      sprintf(errmsg, "fopen error: missing/invalid filetype '%s' specified.\n", s);
      ConsoleWrite(errmsg);
      return NULL;
      }
   else {memcpy(fileid + 8, s, strlen(s)); strcat(filename, s); strcat(filename, " ");}
   s = strtok(NULL, " ");                                                            // get filemode
   if (s == NULL || strlen(s) > 2) {
      sprintf(errmsg, "fopen error: missing/invalid filemode '%s' specified.\n", s);
      ConsoleWrite(errmsg);
      return NULL;
      }
   else {memcpy(fileid + 16, s, strlen(s)); strcat(filename, s);}
   fileid[18] = 0;                                                               // terminate fileid

   if (accessMode & ACCESS_WRITING) {                    // are we writing or appending to the file?
      s = strtok(NULL, " ");                                                    // get record format
      s[0] = toupper(s[0]);
      if (s == NULL || strlen(s) > 1 || (s[0] != 'F' && s[0] != 'V')) {
         sprintf(errmsg, "fopen error: missing/invalid record format '%s' specified.\n", s);
         ConsoleWrite(errmsg);
         return NULL;
         }
      else recfm = s[0];
      s = strtok(NULL, " ");                                             // get record length string
      if (s != NULL) {
         lrecl = strtoul(s, &oops, 10);                                         // get record length
         if (lrecl == 0 || lrecl > 65535) {
            sprintf(errmsg, "fopen error: invalid record length '%s' specified.\n", s);
            ConsoleWrite(errmsg);
            return NULL;
            }
         }
      else if (recfm == 'F') {
         sprintf(errmsg, "fopen error: missing record length.\n", s);
         ConsoleWrite(errmsg);
         return NULL;
         }
      }
   }

// Finally we build the FILE structure and open the file (if required).
switch (devtype) {
   case DEVICE_CON:
      switch (accessMode) {                                    // return the appropriate file handle
         case ACCESS_READ_TXT:                                           // reading from the console
            theFile = theCRAB->consoleInputFile;        // return pointer to file handle in the CRAB
            break;
         case ACCESS_WRITE_TXT:                                            // writing to the console
            theFile = theCRAB->consoleOutputFile;       // return pointer to file handle in the CRAB
            break;
         default:    // can't do binary I/O from/to the console yet, must support this eventually!!!
            sprintf(errmsg, "fopen error: access '%s' for console not yet supported.\n", access);
            ConsoleWrite(errmsg);
            theFile = NULL;
            }
      break;
   case DEVICE_DSK:
      theFile = CMSmemoryAlloc(sizeof(FILE) + lrecl + 1, CMS_USER);                  // + 1 for NULL
      theFile->buffer = &theFile->buffer + 4;                   // point to first byte of I/O buffer
      if (accessMode & ACCESS_WRITING) {                              // are we writing to the file?
         theFile->next = theFile->buffer;                     // nothing in the buffer at this point
         theFile->last = theFile->buffer + lrecl;              // point to 'extra' byte after buffer
         }
      else                                                           // we are reading from the file
         theFile->next = theFile->last = theFile->buffer + lrecl;      // empty buffer at this point
      theFile->eof = 0;                                                            // not yet at EOF
      theFile->error = 0;                                                       // and no errors yet
      theFile->access = accessMode;
      theFile->device = devtype;
      theFile->ungetChar = -2;                              // nothing read, so ungetc not yet valid
      if (accessMode & ACCESS_APPENDING) i = 0;                // appending, so start at next record
      else i = 1;                                         // not appending, so start at first record
      strcpy(theFile->name, filename);       // copy in the filename that will be used in error msgs
      rc = CMSfileOpen(fileid, theFile->buffer, lrecl, recfm, 1, i, &theFile->fscb);
      switch (rc) {                                                        // handle the return code
         case 0:                                                                         // success!
            break;
         case 20:                                             // the fileid is syntactically invalid
            sprintf(errmsg, "fopen error: invalid fileid '%s'.\n", theFile->name);
            ConsoleWrite(errmsg);
            CMSmemoryFree(theFile);                                      // we won't be needing this
            theFile = NULL;
            break;
         case 28:                                                          // the file was not found
            if (accessMode & ACCESS_WRITING) break;               // file not found is OK if writing
            sprintf(errmsg, "fopen error: file '%s' not found.\n", theFile->name);
            ConsoleWrite(errmsg);
            CMSmemoryFree(theFile);                                      // we won't be needing this
            theFile = NULL;
            break;
         }
      break;
   case DEVICE_PRT:
      if (accessMode & ACCESS_WRITING) {
         theFile = CMSmemoryAlloc(sizeof(FILE) + 132 + 1, CMS_USER);                 // + 1 for NULL
         theFile->buffer = &theFile->buffer + 4;                // point to first byte of I/O buffer
         theFile->next = theFile->buffer;                     // nothing in the buffer at this point
         theFile->last = theFile->buffer + 132;                // point to 'extra' byte after buffer
         theFile->last[1] = 0;                                              // add a null terminator
         theFile->eof = 0;                                                         // not yet at EOF
         theFile->error = 0;                                                    // and no errors yet
         theFile->access = accessMode;
         theFile->device = devtype;
         theFile->ungetChar = -2;                           // nothing read, so ungetc not yet valid
         }
      else {
         ConsoleWrite("fopen error: cannot read from the printer.\n");
         theFile = NULL;
         }
      break;
   case DEVICE_PUN:
      if (accessMode & ACCESS_WRITING) {
         theFile = CMSmemoryAlloc(sizeof(FILE) + 80 + 1, CMS_USER);                  // + 1 for NULL
         theFile->buffer = &theFile->buffer + 4;                // point to first byte of I/O buffer
         theFile->next = theFile->buffer;                     // nothing in the buffer at this point
         theFile->last = theFile->buffer + 80;                 // point to 'extra' byte after buffer
         theFile->eof = 0;                                                         // not yet at EOF
         theFile->error = 0;                                                    // and no errors yet
         theFile->access = accessMode;
         theFile->device = devtype;
         theFile->ungetChar = -2;                           // nothing read, so ungetc not yet valid
         }
      else {
         ConsoleWrite("fopen error: cannot read from the card punch.\n");
         theFile = NULL;
         }
      break;
   case DEVICE_RDR:
      if (accessMode & ACCESS_WRITING) {
         ConsoleWrite("fopen error: cannot write to the card reader.\n");
         theFile = NULL;
         }
      else {
         theFile = CMSmemoryAlloc(sizeof(FILE) + 151 + 1, CMS_USER);    // 3211 printer + 1 for NULL
         theFile->buffer = &theFile->buffer + 4;                // point to first byte of I/O buffer
         theFile->next = theFile->last = theFile->buffer + 151;        // empty buffer at this point
         theFile->eof = 0;                                                         // not yet at EOF
         theFile->error = 0;                                                    // and no errors yet
         theFile->access = accessMode;
         theFile->device = devtype;
         theFile->ungetChar = -2;                           // nothing read, so ungetc not yet valid
         break;
         }
   }
return theFile;
}                                                                                    // end of fopen


int fprintf(FILE * stream, const char *format, ...)
/**************************************************************************************************/
/* int fprintf(FILE * stream, const char * format, ...)                                           */
/*                                                                                                */
/* Write formatted output to the specified output stream.                                         */
/*    stream   a pointer to the open output stream.                                               */
/*    format   a string containing characters to be printed and formatting specifications for the */
/*             the variables that follow, if any.                                                 */
/*    ...      0 or more variables that are converted to text and printed according to the        */
/*             formatting specifications in 'format'.                                             */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of characters written to the stream.                                             */
/**************************************************************************************************/
{
int rc;
va_list arg;

if (stream == NULL) {ConsoleWrite("fprintf error: stream is NULL.\n"); return EOF;}
va_start(arg, format);
rc = vvprintf(format, arg, stream, NULL);                          // write the output to the stream
va_end(arg);
return rc;
}                                                                                  // end of fprintf


int fputc(int c, FILE * stream)
/**************************************************************************************************/
/* int fputc(int c, FILE * stream)                                                                */
/*                                                                                                */
/* Write a character to the open output stream. This is the same as putc().                       */
/*    c        the character to be written.                                                       */
/*    stream   a pointer to the open output stream.                                               */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the character, or EOF if there is an error.                                                 */
/**************************************************************************************************/
{
char errmsg[80];
char s[2];
int rc;
int writeRecord;                                   // true if we should write out the current record

if (stream == NULL) {ConsoleWrite("fputc error: stream is NULL.\n"); return EOF;}
switch (stream->device) {
   case DEVICE_CON:
      if (stream->access & ACCESS_WRITE_TXT) {                             // open for writing text?
         s[0] = c; s[1] = 0;
         ConsoleWrite(s);
         }
      else {
         stream->error = 9;
         ConsoleWrite("fputc error: file not open for text output of character '"); // remove
         s[0] = c; s[1] = 0; ConsoleWrite(s); ConsoleWrite("'.\n");
         return EOF;
         }
      break;
   case DEVICE_DSK:
      writeRecord = 0;
      if (stream->access & ACCESS_WRITING) {                                    // open for writing?
         if ((c == '\n') && (stream->access & ACCESS_TEXT)) {          // newline means write record
            writeRecord = 1;
            if (stream->fscb.format[0] == 'F')                // pad fixed length record with blanks
               for (; stream->next < stream->last; stream->next++) stream->next[0] = ' ';
            }
         else {                                                    // not handling newline character
            stream->next[0] = c;
            stream->next = stream->next + 1;
            if (stream->next == stream->last) writeRecord = 1;
            }
         if (writeRecord) {                                          // write the buffer to the file
            stream->error = CMSfileWrite(&stream->fscb, -1, stream->next - stream->buffer);
            if (stream->error != 0) {
               sprintf(errmsg, "fputc error: return code %u from FSWRITE.\n", rc);    // remove this
               ConsoleWrite(errmsg);
               return EOF;
               }
            stream->fscb.recordNum = 0;
            stream->next = stream->buffer;                       // reset the next character pointer
            }
         }
      else {
         stream->error = 9;
         ConsoleWrite("fputc error: file not open for output.\n");   // remove
         return EOF;
         }
      break;
   case DEVICE_PRT:
      if (stream->access & ACCESS_WRITING) {
         stream->next[0] = c;
         stream->next = stream->next + 1;
         if (stream->next == stream->last) {                      // write the buffer to the printer
            stream->next[0] = 0;                                             // terminate the string
            stream->error = CMSprintLine(stream->buffer);
            if (stream->error != 0) {
               sprintf(errmsg, "fputc error: return code %u from PRINTL.\n", rc);     // remove this
               ConsoleWrite(errmsg);
               return EOF;
               }
            stream->next = stream->buffer;                       // reset the next character pointer
            }
         }
      else {
         stream->error = 9;
         ConsoleWrite("fputc error: cannot read from the printer.\n"); // remove this
         return EOF;
         }
      break;
   case DEVICE_PUN:
      if (stream->access & ACCESS_WRITING) {
         stream->next[0] = c;
         stream->next = stream->next + 1;
         if (stream->next == stream->last) {                   // write the buffer to the card punch
            stream->error = CMScardPunch(stream->buffer);
            if (stream->error != 0) {
               sprintf(errmsg, "fputc error: return code %u from PUNCHC.\n", rc);     // remove this
               ConsoleWrite(errmsg);
               return EOF;
               }
            stream->next = stream->buffer;                       // reset the next character pointer
            }
         }
      else {
         stream->error = 9;
         ConsoleWrite("fputc error: cannot read from the card punch.\n"); // removes
         return EOF;
         }
      break;
   case DEVICE_RDR:
      stream->error = 9;
      ConsoleWrite("fputc error: cannot write to the card reader.\n"); // remove this
      return EOF;
      break;
   }
return c;
}                                                                                    // end of fputc


int fputs(const char * str, FILE * stream)
/**************************************************************************************************/
/* int fputs(const char * str, FILE * stream)                                                     */
/*                                                                                                */
/* Write a string to the open output stream.                                                      */
/*    str      the string to be written.                                                          */
/*    stream   a pointer to the open output stream.                                               */
/*                                                                                                */
/* Returns:                                                                                       */
/*    non-negative on success, EOF on failure.                                                    */
/**************************************************************************************************/
{
char * finger;
char c;
int i;
int rc;

if (stream == NULL) {ConsoleWrite("fputs error: stream is NULL.\n"); return EOF;}
switch (stream->device) {
   case DEVICE_CON:
      if (stream->access & ACCESS_WRITE_TXT) {                             // open for writing text?
         i = strlen(str);
         finger = str;
         while (i > 130) {                                                  // write 130-byte chunks
            c = finger[130];
            finger[130] = 0;
            ConsoleWrite(finger);
            finger = finger + 130;
            finger[0] = c;
            i = i + 130;
            }
         ConsoleWrite(finger);                              // write out the remaining characters
         rc = 0;
         }
      else {
         stream->error = 9;
         ConsoleWrite("fputs error: file not open for text output.\n");            // remove this
         return EOF;
         }
      break;
   case DEVICE_DSK:                                  // I promise to fix this code to not call fputc
   case DEVICE_PRT:
   case DEVICE_PUN:
      rc = strlen(str);
      for (i = 0; i < rc; i++) if (fputc(str[i], stream) == EOF) return EOF;
      break;
   case DEVICE_RDR:
      stream->error = 9;
      ConsoleWrite("fputs error: cannot write to the card reader.\n");             // remove this
      return EOF;
      break;
   }
return rc;
}                                                                                    // end of fputs


int xread(void *buffer, size_t size, size_t count, FILE * stream)
/**************************************************************************************************/
/* int fread(void *buffer, size_t size, size_t count, FILE * stream)                              */
/*                                                                                                */
/* Read 'num' objects of size 'size' from the open input stream and write them to 'buffer'.       */
/*    buffer   the array into which the objects are to be read.                                   */
/*    size     the size of each object.                                                           */
/*    count    the number of objects to be read.                                                  */
/*    stream   a pointer to the open input stream.                                                */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of objects read.                                                                 */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  Use fread for writing binary data.  Newlines in the input stream are treated as any     */
/*        other character.                                                                        */
/*    2.  When the end of a record is reached, reading continues uninterrupted from the next      */
/*        record.  Thus the file is treated as a stream of bytes.                                 */
/**************************************************************************************************/
{
char c;
char * finger;
int i;
int j;

if (stream == NULL) {ConsoleWrite("fread error: stream is NULL.\n"); return EOF;}
switch (stream->device) {
   case DEVICE_CON:
      break;                                                                    // not yet supported
   case DEVICE_DSK:                                   // I promise to fix this code to not call getc
   case DEVICE_RDR:
// check open for reading!!!!!!!!!!!!!!!!!!!!!
      finger = (char *) buffer;
      for (i = 0; i < count; i++) {
         for (j = 0; j < size; j++) {
            c = getc(stream);                      // some day I will fix this to avoid calling getc
            if (c == EOF) return i;                                    // end of file or other error
            finger[0] = c;
            finger++;
            }
         }
      break;
   case DEVICE_PRT:
      stream->error = 9;
      ConsoleWrite("fread error: cannot read from the printer.\n");                // remove this
      return 0;
      break;
   case DEVICE_PUN:
      stream->error = 9;
      ConsoleWrite("fread error: cannot read from the card punch.\n");             // remove this
      return 0;
      break;
   }
return i;
}                                                                                    // end of fread


FILE * freopen(const char * filespec, const char * access, FILE * stream)
/**************************************************************************************************/
/* FILE * freopen(const char * filespec, const char * access, FILE * stream)                      */
/*                                                                                                */
/* This function does nothing.                                                                    */
/*                                                                                                */
/* Returns:                                                                                       */
/*    NULL, indicating the request could not be satisfied.                                        */
/**************************************************************************************************/
{
return NULL;
}                                                                                  // end of freopen


int fscanf(FILE * stream, const char * format, ...)
/**************************************************************************************************/
/* int fscanf(FILE * stream, const char * format, ...)                                            */
/*                                                                                                */
/* Read formatted input from the specified input stream and store the converted values in the     */
/* specified variables.                                                                           */
/*    stream   a pointer to the open output stream.                                               */
/*    format   a string containing the conversion specifications for the incoming characters to   */
/*             the variables that follow, if any.                                                 */
/*    ...      0 or more pointers to variables that receive the values converted from text        */
/*             according to the formatting specifications in 'format'.                            */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of variables successfully assigned values.                                       */
/**************************************************************************************************/
{
va_list arg;
int rc;

if (stream == NULL) {ConsoleWrite("fscanf error: stream is NULL.\n"); return EOF;}
va_start(arg, format);
rc = vvscanf(format, arg, stream, NULL);
va_end(arg);
return rc;
}                                                                                       // of fscanf


long ftell(FILE * stream)
/**************************************************************************************************/
/* long ftell(FILE * stream)                                                                      */
/*                                                                                                */
/* Return the current file position for the specified stream.                                     */
/*    stream   a pointer to the open stream.                                                      */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the position of the next character to be read or written.                                   */
/**************************************************************************************************/
{
return (stream->next - stream->buffer);
}                                                                                    // end of ftell


int fwrite(const void * buffer, size_t size, size_t count, FILE * stream)
/**************************************************************************************************/
/* int fwrite(const void * buffer, size_t size, size_t count, FILE * stream)                      */
/*                                                                                                */
/* Write 'count' objects of size 'size' from 'buffer' to the open output stream.                  */
/*    buffer   the array of objects to be written.                                                */
/*    size     the size of each object.                                                           */
/*    count    the number of objects to be written.                                               */
/*    stream   a pointer to the open output stream.                                               */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of objects written.                                                              */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  Use fwrite for writing binary data.  Newlines in the output stream are treated as any   */
/*        other character.                                                                        */
/*    2.  When the record length for the file is reached, that record is written to disk and a    */
/*        new record is started.  Thus the file is treated as a stream of bytes.                  */
/**************************************************************************************************/
{
char * finger;
int i;
int j;

if (stream == NULL) {ConsoleWrite("fwrite error: stream is NULL.\n"); return EOF;}
switch (stream->device) {
   case DEVICE_CON:
      break;                                                                    // not yet supported
   case DEVICE_DSK:                                   // I promise to fix this code to not call putc
   case DEVICE_PRT:
   case DEVICE_PUN:
// check for writing!!!!!!!!!!!!!!!!!!!
      finger = (char *) buffer;
      for (i = 0; i < count; i++) {
         for (j = 0; j < size; j++) {
            if (putc(finger[0], stream) == EOF) return i;
            finger++;
            }
         }
      break;
   case DEVICE_RDR:
      stream->error = 9;
      ConsoleWrite("fwwrite error: cannot write to the card reader.\n");           // remove this
      return 0;
      break;
   }
return i;
}                                                                                   // end of fwrite


int getc(FILE * stream)
/**************************************************************************************************/
/* int getc(FILE * stream)                                                                        */
/*                                                                                                */
/* Read the next character from the specified output stream. This is identical to fgetc().        */
/*    stream   a pointer to the open output stream.                                               */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the character, or EOF if there is an error.                                                 */
/**************************************************************************************************/
{
return fgetc(stream);
}                                                                                     // end of getc


int getchar(void)
/**************************************************************************************************/
/* int getchar(void)                                                                              */
/*                                                                                                */
/* Read the next character from the console.                                                      */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the character, or EOF if there is an error.                                                 */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  If there is no open FILE for console input, we open it and store the handle in the GCC  */
/*        stack.  This memory will be freed in CSTART ASSEMBLE.                                   */
/**************************************************************************************************/
{
CMSCRAB * theCRAB;

__asm__(LOADCRAB : "=d" (theCRAB));                                           // get address of CRAB
return getc(theCRAB->consoleInputFile);                           // get the character and return it
}                                                                                  // end of getchar


char * gets(char * str)
/**************************************************************************************************/
/* char * gets(char * str)                                                                        */
/*                                                                                                */
/* Read characters from the console and place them into 'str', terminating them with a NULL.      */
/* gets() stops when it reaches the end of a line, in which case 'str' will contain a newline     */
/* character.  fgets() stops when it encounters the EOF character.                                */
/*                                                                                                */
/* Returns:                                                                                       */
/*    a pointer to 'str', or EOF if there is an error.                                            */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  If there is no open FILE for console input, we open it and store the handle in the GCC  */
/*        stack.  This memory will be freed in CSTART ASSEMBLE.                                   */
/**************************************************************************************************/
{
CMSCRAB * theCRAB;

__asm__(LOADCRAB : "=d" (theCRAB));                                           // get address of CRAB
return fgets(str, 131, theCRAB->consoleInputFile);                   // get the string and return it
}                                                                                     // end of gets


int printf(const char * format, ...)
/**************************************************************************************************/
/* int printf(const char * format, ...)                                                           */
/*                                                                                                */
/* Write formatted output on the CMS console.  Note that this native implementation does not      */
/* support redirection to other files or devices.                                                 */
/*    format   a string containing characters to be printed and formatting specifications for the */
/*             the variables that follow, if any.                                                 */
/*    ...      0 or more variables that are converted to text and printed according to the        */
/*             formatting specifications in 'format'.                                             */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of characters printed.                                                           */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  If there is no open FILE for console output, we open it and store the handle in the GCC */
/*        stack.  This memory will be freed in CSTART ASSEMBLE.                                   */
/**************************************************************************************************/
{
CMSCRAB * theCRAB;
int rc;
va_list arg;

__asm__(LOADCRAB : "=d" (theCRAB));                                           // get address of CRAB
if (theCRAB->consoleOutputFile == NULL) {
   __debug(33);
   ConsoleWrite("NULL console file handle in printf!\n");
   return 0;
   }
va_start(arg, format);
rc = vvprintf(format, arg, theCRAB->consoleOutputFile, NULL);
va_end(arg);
return rc;
}                                                                                   // end of printf


int putc(int c, FILE * stream)
/**************************************************************************************************/
/* int putc(int c, FILE * stream)                                                                 */
/*                                                                                                */
/* Write a character to the specified output stream.                                              */
/*    c        the character to be written.                                                       */
/*    stream   a pointer to the open output stream.                                               */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the character, or EOF if there is an error.                                                 */
/**************************************************************************************************/
{
return fputc(c, stream);
}                                                                                     // end of putc


int putchar(int c)
/**************************************************************************************************/
/* int putchar(int c)                                                                             */
/*                                                                                                */
/* Write a character to the console.                                                              */
/*    c        the character to be written.                                                       */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the character, or EOF if there is an error.                                                 */
/**************************************************************************************************/
{
char s[2];

s[0] = c; s[1] = 0;
ConsoleWrite(s);                                                      // write it to the terminal
}                                                                                  // end of putchar


int puts(char * s)
/**************************************************************************************************/
/* int fputs(const char * s)                                                                      */
/*                                                                                                */
/* write a string to the console.                                                                 */
/*    s        the string to be written.                                                          */
/*                                                                                                */
/* Returns:                                                                                       */
/*    non-negative on success, EOF on failure.                                                    */
/**************************************************************************************************/
{
char * finger;
char c;
int i;
int rc;

i = strlen(s);
finger = s;
while (i > 130) {                                                           // write 130-byte chunks
   c = finger[130];
   finger[130] = 0;
   ConsoleWrite(finger);
   finger = finger + 130;
   finger[0] = c;
   i = i + 130;
   }
ConsoleWrite(finger);
return 0;
}                                                                                     // end of puts


int remove(const char * fname)
/**************************************************************************************************/
/* int remove(const char * fname)                                                                 */
/*                                                                                                */
/* Erases the specified file.                                                                     */
/*    fname    a string specifying the file to be erased.  It is comprised of three words,        */
/*             separated by 1 or more blanks:                                                     */
/*                filename is the up to 8 character name.                                         */
/*                filetype is the up to 8 character type.                                         */
/*                filemode is the up to 2 character disk mode leter and optional number.          */
/*                                                                                                */
/* Returns:                                                                                       */
/*    0 if the file was erased, otherwise the return code from fserase.                           */
/**************************************************************************************************/
{
char errmsg[80];
char fileid[19];

if (GetFileid(fname, fileid) == 0) {                       // parse the filename, filetype, filemode
   sprintf(errmsg, "remove error: missing/invalid fileid '%s' specified.\n", fname);
   ConsoleWrite(errmsg);
   return NULL;
   }
return CMSfileErase(fileid);
}                                                                                   // end of remove


int rename(const char * oldfname, const char * newfname)
/**************************************************************************************************/
/* int rename(const char * oldfname, const char * newfname)                                       */
/*                                                                                                */
/* Renames file 'oldfname' to 'newfname'.  'oldfname' and 'newfname' are strings specifying the   */
/* old and new name for the file.  They are comprised of three words, separated by 1 or more      */
/* blanks:                                                                                        */
/*    filename is the up to 8 character name.                                                     */
/*    filetype is the up to 8 character type.                                                     */
/*    filemode is the up to 2 character disk mode leter and optional number.                      */
/*                                                                                                */
/* Returns:                                                                                       */
/*    0 if the file was renamed, otherwise the return code from fsrename.                         */
/**************************************************************************************************/
{
char errmsg[80];
char newfileid[19];
char oldfileid[19];

if (GetFileid(oldfname, oldfileid) == 0) {                 // parse the filename, filetype, filemode
   sprintf(errmsg, "rename error: missing/invalid fileid '%s' specified for 'oldfname'.\n",
      oldfname);
   ConsoleWrite(errmsg);
   return NULL;
   }
if (GetFileid(newfname, newfileid) == 0) {                 // parse the filename, filetype, filemode
   sprintf(errmsg, "rename error: missing/invalid fileid '%s' specified for 'newfname'.\n",
      newfname);
   ConsoleWrite(errmsg);
   return NULL;
   }
return CMSfileRename(oldfileid, newfileid);
}                                                                                   // end of rename


int scanf(const char * format, ...)
/**************************************************************************************************/
/* int scanf(const char * format, ...)                                                            */
/*                                                                                                */
/* Read formatted input from the console and store the converted values in the specified          */
/* variables.                                                                                     */
/*    format   a string containing the conversion specifications for the incoming characters to   */
/*             the variables that follow, if any.                                                 */
/*    ...      0 or more pointers to variables that receive the values converted from text        */
/*             according to the formatting specifications in 'format'.                            */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of variables successfully assigned values.                                       */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  If there is no open FILE for console input, we open it and store the handle in the GCC  */
/*        stack.  This memory will be freed in CSTART ASSEMBLE.                                   */
/**************************************************************************************************/
{
CMSCRAB * theCRAB;
int rc;
va_list arg;

__asm__(LOADCRAB : "=d" (theCRAB));                                           // get address of CRAB
va_start(arg, format);
rc = vvscanf(format, arg, theCRAB->consoleInputFile, NULL);
va_end(arg);
return rc;
}                                                                                    // end of scanf


void setbuf(FILE * stream, char * buffer)
/**************************************************************************************************/
/* void setbuf(FILE * stream, char *buffer)                                                       */
/*                                                                                                */
/* This function does nothing.                                                                    */
/**************************************************************************************************/
{
return;
}                                                                                   // end of setbuf


int setvbuf(FILE * stream, char * buffer, int mode, size_t size)
/**************************************************************************************************/
/* int setvbuf(FILE * stream, char * buffer, int mode, size_t size)                               */
/*                                                                                                */
/* This function does nothing.                                                                    */
/*                                                                                                */
/* Returns:                                                                                       */
/*    1, indicating that the request cannot be satistifed.                                        */
/**************************************************************************************************/
{
return 1;
}                                                                                  // end of setvbuf


int sprintf(char * buffer, const char * format, ...)
/**************************************************************************************************/
/* int sprintf(char * buffer, const char *format, ...)                                            */
/*                                                                                                */
/* Write formatted output to a string.                                                            */
/*    buffer   a pointer to a buffer into which the formatted output is copied.                   */
/*    format   a string containing characters to be printed and formatting specifications for the */
/*             the variables that follow, if any.                                                 */
/*    ...      0 or more variables that are converted to text and printed according to the        */
/*             formatting specifications in 'format'.                                             */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of characters written to the string.                                             */
/**************************************************************************************************/
{
int rc;
va_list arg;

va_start(arg, format);
rc = vvprintf(format, arg, NULL, buffer);                          // build the string we will print
if (rc >= 0) buffer[rc] = 0;                                                 // terminate the string
va_end(arg);
return (rc);
}                                                                                  // end of sprintf


int sscanf(const char * buffer, const char * format, ...)
/**************************************************************************************************/
/* int sscanf(const char * buffer, const char * format, ...)                                      */
/*                                                                                                */
/* Read formatted input from a buffer and store the converted values in the specified variables.  */
/*    buffer   a string containing the text to be converted to values.                            */
/*    format   a string containing the conversion specifications for the incoming characters to   */
/*             the variables that follow, if any.                                                 */
/*    ...      0 or more pointers to variables that receive the values converted from text        */
/*             according to the formatting specifications in 'format'.                            */
/*                                                                                                */
/* Returns:                                                                                       */
/*    c if successful, EOF if not.                                                                */
/**************************************************************************************************/
{
int rc;
va_list arg;

va_start(arg, format);
rc = vvscanf(format, arg, NULL, buffer);
va_end(arg);
return rc;
}                                                                                   // end of sscanf


FILE * tmpfile(void)
/**************************************************************************************************/
/* FILE * tmpfile(void)                                                                           */
/*                                                                                                */
/* This function does nothing.                                                                    */
/*                                                                                                */
/* Returns:                                                                                       */
/*    NULL, indicating that the request cannot be satistifed.                                     */
/**************************************************************************************************/
{
return NULL;
}                                                                                  // end of tmpfile


char * tmpnam(char * name)
/**************************************************************************************************/
/* char * tmpnam(char * name)                                                                     */
/*                                                                                                */
/* Returns a temporary fileid (filename filetype A1) that is in all likelihood unique.            */
/**************************************************************************************************/
{
char clock[8];
struct tm * t;
time_t theTime;

time(&theTime);
t = localtime(&theTime);
strftime(name, 14, "T%y%j%H %M%S", t);
CMSclock(clock);
sprintf(clock, "%02X%02X A1", clock[4], clock[5]);              // add 2 bytes from the system clock
strcat(name, clock);
return name;
}                                                                                   // end of tmpnam


int ungetc(int c, FILE * stream)
/**************************************************************************************************/
/* int ungetc(int c, FILE * stream)                                                               */
/*                                                                                                */
/* Push a character back onto the open input stream 'stream', such that it is the next character  */
/* to be read.                                                                                    */
/*    c        the character to be pushed back onto the stream.                                   */
/*    stream   a pointer to the open input stream.                                                */
/*                                                                                                */
/* Returns:                                                                                       */
/*    c if successful, EOF if not.                                                                */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  Only 1 character can be in the unget buffer.  If a second call to ungetc is made before */
/*        the previous character is read, it is not placed in the buffer and EOF is returned.     */
/**************************************************************************************************/
{
if (stream->ungetChar == -1) {                                       // OK to push back a character?
   stream->ungetChar = c;
   return c;
   }
else return EOF;
}                                                                                   // end of ungetc


int vfprintf(FILE * stream, const char *format, va_list arg)
/**************************************************************************************************/
/* int vfprintf(FILE * stream, const char * format, va_list arg)                                  */
/*                                                                                                */
/* Write formatted output to the specified output stream.                                         */
/*    stream   a pointer to the open output stream.                                               */
/*    format   a string containing characters to be printed and formatting specifications for the */
/*             the variables that follow, if any.                                                 */
/*    arg      the argument list.                                                                 */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of characters written to the stream.                                             */
/**************************************************************************************************/
{
int ret;

ret = vvprintf(format, arg, stream, NULL);
return ret;
}                                                                                 // end of vfprintf


int vsprintf(char * buffer, const char * format, va_list arg)
/**************************************************************************************************/
/* int vsprintf(char * buffer, const char * format, va_list arg)                                  */
/*                                                                                                */
/* Write formatted output to a string using an argument list.                                     */
/*    buffer   a pointer to a buffer into which the formatted output is copied.                   */
/*    format   a string containing characters to be printed and formatting specifications for the */
/*             the variables that follow, if any.                                                 */
/*    arg      the argument list.                                                                 */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of characters printed.                                                           */
/**************************************************************************************************/
{
int ret;

ret = vvprintf(format, arg, NULL, buffer);
if (ret >= 0) buffer[ret] = 0;                                            // ensure null termination
return ret;
}                                                                                 // end of vsprintf


int notyet(void)
/**************************************************************************************************/
/* Emit a message when runtime routines not yet implemented are called.                           */
/*                                                                                                */
/* Returns:                                                                                       */
/*    -1                                                                                          */
/**************************************************************************************************/
{
ConsoleWrite("This routine is not yet implemented...\n");
return -1;
}                                                                                   // end of notyet


/*------------------------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------------------------*/
/* Internal routines.                                                                             */
/*------------------------------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------------------------*/


static void dblcvt(double num, char cnvtype, size_t nwidth, size_t nprecision, char *result)
/**************************************************************************************************/
/* This truly cludged piece of code was concocted by Dave Wade.  His erstwhile tutors are         */
/* probably turning in their graves.  It is however placed in the Public Domain so that any one   */
/* who wishes to improve is free to do so.                                                        */
/**************************************************************************************************/
{
double b,round;
int i,j,exp,pdigits,format;
char sign, work[45];

if ( num < 0 ) { b = -num; sign = '-'; }                            // save original data & set sign
else { b = num; sign = ' '; }


exp = 0;                                                                // now scale to get exponent
if ( b > 1.0 ) while (b >= 10.0) { ++exp; b=b / 10.0; }
else if ( b == 0.0 ) exp=0;
else if ( b < 1.0 ) while (b < 1.0) { --exp; b=b*10.0; }

// Now decide how to print and save in FORMAT.
//    -1 => we need leading digits.
//     0 => print in exp.
//    +1 => we have digits before dp.
switch (cnvtype) {
   case 'E':
   case 'e':
      format = 0;
      break;
   case 'f':
   case 'F':
      if ( exp >= 0 ) format = 1; else format = -1;
      break;
   default:
      // Style e is used if the exponent from its conversion is less than -4 or greater than or
      // equal to the precision.
      if ( exp >= 0 ) {
         if ( nprecision > exp ) format=1;
         else format=0;
         }
      else {                                                    // if ( nprecision > (-(exp+1) ) ) {
         if ( exp >= -4) format=-1;
         else format=0;
         }
      break;
   }


switch (format) {                                                                       // now round
   case 0:                                                       // we are printing in standard form
      if (nprecision < DBL_MANT_DIG) j = nprecision;                             // we need to round
      else j = DBL_MANT_DIG;
      round = 1.0/2.0;
      i = 0;
      while (++i <= j) round = round/10.0;
      b = b + round;
      if (b >= 10.0) { b = b/10.0; exp = exp + 1; }
      break;
   case 1:                     // we have a number > 1: need to round at the exp + nprescionth digit
      if (exp + nprecision < DBL_MANT_DIG) j = exp + nprecision;                 // we need to round
      else j = DBL_MANT_DIG;
      round = 0.5;
      i = 0;
      while (i++ < j) round = round/10;
      b = b + round;
      if (b >= 10.0) { b = b/10.0; exp = exp + 1; }
      break;
   case -1:                                                   // we have a number that starts 0.xxxx
      if (nprecision < DBL_MANT_DIG) j = nprecision + exp + 1;                   // we need to round
      else j = DBL_MANT_DIG;
      round = 5.0;
      i = 0;
      while (i++ < j) round = round/10;
      if (j >= 0) b = b + round;
      if (b >= 10.0) { b = b/10.0; exp = exp + 1; }
      if (exp >= 0) format = 1;
      break;
   }

   // Now extract the requisite number of digits
if (format == -1) {               // number < 1.0 so we need to print the "0." and the leading zeros
   result[0]=sign; result[1]='0'; result[2]='.'; result[3]=0x00;
   while (++exp) { --nprecision; strcat(result,"0"); }
   i=b;
   --nprecision;
   work[0] = (char) ('0' + i % 10); work[1] = 0x00;
   strcat(result, work);
   pdigits = nprecision;
   while (pdigits-- > 0) {
      b = b - i;
      b = b * 10.0;
      i = b;
      work[0] = (char) ('0' + i % 10); work[1] = 0x00;
      strcat(result,work);
      }
   }
else if (format==+1) {                                   // number >= 1.0 just print the first digit
   i = b;
   result[0] = sign; result[1] = '\0';
   work[0] = (char) ('0' + i % 10); work[1] = 0x00;
   strcat(result, work);
   nprecision = nprecision + exp;
   pdigits = nprecision ;
   while (pdigits-- > 0) {
      if ( ((nprecision-pdigits-1) == exp) ) strcat(result, ".");
      b = b - i;
      b = b * 10.0;
         // The following test needs to be adjusted to allow for numeric fuzz.
      if (((nprecision - pdigits - 1) > exp) && (b < 0.1E-15)) {
         if (cnvtype != 'G' && cnvtype != 'g') strcat(result,"0");
         }
      else {
         i = b;
         work[0] = (char)('0' + i % 10); work[1] = 0x00;
         strcat(result, work);
         }
      }
   }
else {                                                                  // printing in standard form
   i = b;
   result[0] = sign; result[1] = '\0';
   work[0] = (char)('0' + i % 10); work[1] = 0x00;
   strcat(result,work);
   strcat(result,".");
   pdigits = nprecision;
   while (pdigits-- > 0) {
      b = b - i;
      b = b * 10.0;
      i = b;
      work[0] = (char)('0' + i % 10); work[1] = 0x00;
      strcat(result,work);
      }
   }
if (format==0) {                                                      // exp format - put exp on end
   work[0] = 'E';
   if ( exp < 0 ) {
      exp = -exp;
      work[1]= '-';
      }
   else work[1]= '+';
   work[2] = (char) ('0' + (exp / 10) % 10); work[3] = (char) ('0' + exp % 10); work[4] = 0x00;
   strcat(result, work);
   }
   // printf(" Final Answer = <%s> fprintf goves=%g\n", result,num);
   //  do we need to pad?
if (result[0] == ' ') strcpy(work, result + 1); else strcpy(work, result);
pdigits = nwidth-strlen(work);
result[0] = 0x00;
while(pdigits>0) { strcat(result," "); pdigits--; }
strcat(result, work);
return;
}                                                                                   // end of dblcvt


static int examine(const char **formt, FILE *fq, char *s, va_list *arg, int chcount)
/**************************************************************************************************/
/* Part of vvprintf... could be inline, really.                                                   */
/**************************************************************************************************/
{
int extraCh = 0;
int flagMinus = 0;
int flagPlus = 0;
int flagSpace = 0;
int flagHash = 0;
int flagZero = 0;
int width = 0;
int precision = -1;
int half = 0;
int lng = 0;
int specifier = 0;
int fin;
long lvalue;
short int hvalue;
int ivalue;
unsigned long ulvalue;
double vdbl;
char *svalue;
char work[50];
int x;
int y;
int rem;
const char *format;
int base;
int fillCh;
int neg;
int length;
size_t slen;

unused(chcount);
format = *formt;
fin = 0;
while (! fin) {
   switch (*format) {                                                               // process flags
      case '-': flagMinus = 1; break;
      case '+': flagPlus = 1; break;
      case ' ': flagSpace = 1; break;
      case '#': flagHash = 1; break;
      case '0': flagZero = 1; break;
      case '*': width = va_arg(*arg, int); break;
      default:  fin = 1; break;
      }
   if (! fin) format++;
   else {
      if (flagSpace && flagPlus) flagSpace = 0;
      if (flagMinus) flagZero = 0;
      }
   }

if (isdigit((unsigned char)*format)) while (isdigit((unsigned char)*format)) {      // process width
   width = width * 10 + (*format - '0');
   format++;
   }

if (*format == '.') {                                                           // process precision
   format++;
   if (*format == '*') {
      precision = va_arg(*arg, int);
      format++;
      }
   else {
      precision = 0;
      while (isdigit((unsigned char)*format)) {
         precision = precision * 10 + (*format - '0');
         format++;
         }
      }
   }

if (*format == 'h') {                                                               // process h/l/L
      // all environments should promote shorts to ints, so we should be able to ignore the 'h'
      // specifier.  It will create problems otherwise.
      // half = 1;
   }
else if (*format == 'l') lng = 1;
else if (*format == 'L') lng = 1;
else format--;
format++;

specifier = *format;                                                            // process specifier
if (strchr("dxXuiop", specifier) != NULL && specifier != 0) {
   if (precision < 0) precision = 1;
   if (lng) lvalue = va_arg(*arg, long);
   else if (half) {
      hvalue = va_arg(*arg, short);
      if (specifier == 'u') lvalue = (unsigned short)hvalue;
      else lvalue = hvalue;
      }
    else {
      ivalue = va_arg(*arg, int);
      if (specifier == 'u') lvalue = (unsigned int)ivalue;
      else lvalue = ivalue;
      }
   ulvalue = (unsigned long)lvalue;
   if ((lvalue < 0) && ((specifier == 'd') || (specifier == 'i'))) {
      neg = 1;
      ulvalue = -lvalue;
      }
   else neg = 0;
   if ((specifier == 'X') || (specifier == 'x') || (specifier == 'p')) base = 16;
   else if (specifier == 'o') base = 8;
   else base = 10;
   if (specifier == 'p') { }
   x = 0;
   while (ulvalue > 0) {
      rem = (int)(ulvalue % base);
      if (rem < 10) work[x] = (char)('0' + rem);
      else {
         if ((specifier == 'X') || (specifier == 'p')) work[x] = (char)('A' + (rem - 10));
         else work[x] = (char)('a' + (rem - 10));
         }
      x++;
      ulvalue = ulvalue / base;
      }
   while (x < precision) {
      work[x] = '0';
      x++;
      }
   if (neg) work[x++] = '-';
   else if (flagPlus) work[x++] = '+';
   if (flagZero) fillCh = '0';
   else fillCh = ' ';
   y = x;
   if (!flagMinus) while (y < width) {
      outch(fillCh);
      extraCh++;
      y++;
      }
   if (flagHash && (toupper((unsigned char)specifier) == 'X')) {
      outch('0');
      outch('x');
      extraCh += 2;
      }
   x--;
   while (x >= 0) {
      outch(work[x]);
      extraCh++;
      x--;
      }
   if (flagMinus) while (y < width) {
      outch(fillCh);
      extraCh++;
      y++;
      }
   }
else if (strchr("eEgGfF", specifier) != NULL && specifier != 0) {
   if (precision < 0) precision = 6;
   vdbl = va_arg(*arg, double);
   dblcvt(vdbl, specifier, width, precision, work);                                  // 'e','f' etc.
   slen = strlen(work);
   if (fq == NULL) {
      memcpy(s, work, slen);
      s += slen;
      }
   else fputs(work, fq);
   extraCh += slen;
   }
else if (specifier == 's') {
   if (precision < 0) precision = 1;
   svalue = va_arg(*arg, char *);
   fillCh = ' ';
   if (precision > 1) {
      char *p;

      p = memchr(svalue, '\0', precision);
      if (p != NULL) length = (int)(p - svalue);
      else length = precision;
      }
   else length = strlen(svalue);
   if (!flagMinus) {
      if (length < width) {
         extraCh += (width - length);
         for (x = 0; x < (width - length); x++) outch(fillCh);
         }
      }
   for (x = 0; x < length; x++) outch(svalue[x]);
   extraCh += length;
   if (flagMinus) {
      if (length < width) {
         extraCh += (width - length);
         for (x = 0; x < (width - length); x++) outch(fillCh);
         }
      }
   }
*formt = format;
return (extraCh);
}                                                                                  // end of examine

int GetFileid(const char * fname, char * fileid)
/**************************************************************************************************/
/* int GetFileid(const char * fname, char * fileid)                                               */
/*                                                                                                */
/* Parse the specified CMS filename / filetype / filemode in 'fname', filling in 'fileid' for use */
/* in other routines such as fopen, remove, rename.                                               */
/*                                                                                                */
/* Returns 1 if successful, 0 if not.                                                             */
/**************************************************************************************************/
{
char * s;
char fileinfo[40];
char errmsg[80];

strncpy(fileinfo, fname, sizeof(fileinfo));                // because strtok modifies the string :-(
memset(fileid, ' ', 18);                                              // initialize fileid to blanks
s = strtok(fileinfo, " ");                                                           // get filename
if (s == NULL || strlen(s) > 8) {
   sprintf(errmsg, "GetFileid error: missing/invalid filename '%s' specified.\n", s);
   ConsoleWrite(errmsg);
   return 0;
   }
else memcpy(fileid, s, strlen(s));
s = strtok(NULL, " ");                                                               // get filetype
if (s == NULL || strlen(s) > 8) {
   sprintf(errmsg, "GetFileid error: missing/invalid filetype '%s' specified.\n", s);
   ConsoleWrite(errmsg);
   return 0;
   }
else memcpy(fileid + 8, s, strlen(s));
s = strtok(NULL, " ");                                                               // get filemode
if (s == NULL || strlen(s) > 2) {
   sprintf(errmsg, "GetFileid error: missing/invalid filemode '%s' specified.\n", s);
   ConsoleWrite(errmsg);
   return 0;
   }
else memcpy(fileid + 16, s, strlen(s));
fileid[18] = 0;                                          // terminate fileid (not strictly required)
return 1;                                                                        // indicate success
}                                                                                // end of GetFileid


static int vvprintf(const char *format, va_list arg, FILE *fq, char *s)
/**************************************************************************************************/
/* static int vvprintf(const char *format, va_list arg, FILE *fq, char *s)                        */
/*
/* Do the real work of printf.                                                                    */
/*    format   a string containing characters to be printed and formatting specifications for the */
/*             the variables that follow, if any.                                                 */
/*    va_list  a list of variables that are converted to text and printed according to the        */
/*             formatting specifications in 'format'.                                             */
/*    fq       a pointer to a FILE handle to which the formatted output is written.               */
/*    s        a pointer to a buffer into which the formatted output is copied.                   */
/*                                                                                                */
/* Returns:                                                                                       */
/*    the number of characters written.                                                           */
/*                                                                                                */
/* Notes:                                                                                         */
/*    1.  Either 'fq' or 's' must be NULL.                                                        */
/**************************************************************************************************/
{
int fin = 0;
int vint;
double vdbl;
unsigned int uvint;
char *vcptr;
int chcount = 0;
size_t len;
char numbuf[50];
char *nptr;

while (! fin) {
   if (*format == '\0') fin = 1;
   else if (*format == '%') {
      format++;
      if (* format == 'd') {
         vint = va_arg(arg, int);
         if (vint < 0) uvint = -vint;
         else uvint = vint;
         nptr = numbuf;
         do {
            *nptr++ = (char)('0' + uvint % 10);
            uvint /= 10;
            } while (uvint > 0);
         if (vint < 0) *nptr++ = '-';
         do {
            nptr--;
            outch(*nptr);
            chcount++;
            } while (nptr != numbuf);
         }
      else if (strchr("eEgGfF", *format) != NULL && *format != 0) {
         vdbl = va_arg(arg, double);
         dblcvt(vdbl, *format, 0, 6, numbuf);                                       // 'e','f' etc.
         len = strlen(numbuf);
         if (fq == NULL) {
            memcpy(s, numbuf, len);
            s += len;
            }
         else fputs(numbuf, fq);
         chcount += len;
         }
      else if (*format == 's') {
         vcptr = va_arg(arg, char *);
         if (vcptr == NULL) vcptr = "(null)";
         if (fq == NULL) {
            len = strlen(vcptr);
            memcpy(s, vcptr, len);
            s += len;
            chcount += len;
            }
         else {
            fputs(vcptr, fq);
            chcount += strlen(vcptr);
            }
         }
      else if (*format == 'c') {
         vint = va_arg(arg, int);
         outch(vint);
         chcount++;
         }
      else if (*format == '%') {
         outch('%');
         chcount++;
         }
      else {
         int extraCh;

         extraCh = examine(&format, fq, s, &arg, chcount);
         chcount += extraCh;
         if (s != NULL) s += extraCh;
         }
      }
   else {
      outch(*format);
      chcount++;
      }
   format++;
   }
return (chcount);
}                                                                                 // end of vvprintf


static int vvscanf(const char *format, va_list arg, FILE *fp, const char *s)
/**************************************************************************************************/
/* vvscanf - the guts of the input scanning                                                       */
/* several mods by Dave Edwards                                                                   */
/**************************************************************************************************/
{
int ch;
int fin = 0;
int cnt = 0;
char *cptr;
int *iptr;
unsigned int *uptr;
long *lptr;
unsigned long *luptr;
short *hptr;
unsigned short *huptr;
double *dptr;
float *fptr;
long startpos;
const char *startp;
int skipvar;                                             // nonzero if we are skipping this variable
int modlong;                                                        // nonzero if "l" modifier found
int modshort;                                                       // nonzero if "h" modifier found
int informatitem;                                                // nonzero if % format item started
// informatitem is 1 if we have processed "%l" but not the type letter (s,d,e,f,g,...) yet

if (fp != NULL) startpos = ftell(fp);
else startp = s;
inch();
informatitem = 0;                                                                            // init
if ((fp != NULL && ch == EOF) || (fp == NULL && ch == 0)) return EOF;     // at EOF or end of string
while (! fin) {
   if (*format == '\0') fin = 1;
   else if (*format == '%' || informatitem) {
      if (*format == '%') {                                                // starting a format item
         format++;
         modlong=0;                                                                          // init
         modshort=0;
         skipvar = 0;
         if (*format == '*') {
            skipvar = 1;
            format++;
            }
         }
         if (*format == '%') {                                                                 // %%
            if (ch != '%') return (cnt);
            inch();
            informatitem=0;
            }
         else if (*format == 'l') {                            // type modifier: l (e.g. %ld or %lf)
            modlong=1;
            informatitem=1;
            }
         else if (*format == 'h') {                                  // type modifier: h (short int)
            modshort=1;
            informatitem=1;
            }
         else {                                                          // process a type character
            informatitem=0;                                                    // end of format item
            if (*format == 's') {
               if (!skipvar) cptr = va_arg(arg, char *);
               while (ch>=0 && isspace(ch)) inch();                       // skip leading whitespace
               if ((fp != NULL && ch == EOF) || (fp == NULL && ch == 0)) {   // EOF or end of string
                  fin = 1;
                  if (!skipvar) *cptr = 0;                                     // give a null string
                  }
               else {
                  for (;;) {
                     if (isspace(ch)) break;
                     if ((fp != NULL && ch == EOF) || (fp == NULL && ch == 0)) {
                        fin = 1;
                        break;
                        }
                     if (!skipvar) *cptr++ = (char)ch;
                     inch();
                     }
                  if (!skipvar) *cptr = '\0';
                  cnt++;
                  }
               }
            else if (*format == '[') {
               int reverse = 0;
               int found;
               const char *first;
               const char *last;
               size_t size;
               size_t mcnt = 0;

               if (!skipvar) cptr = va_arg(arg, char *);
               format++;
               if (*format == '?') {
                  reverse = 1;
                  format++;
                  }
               if (*format == '\0') break;
               first = format;
               format++;
               last = strchr(format, ']');
               if (last == NULL) return (cnt);
               size = last - first;
                  // Note: C90 doesn't require special processing for '-' so it hasn't been added.
               while (1) {
                  found = (memchr(first, ch, size) != NULL);
                  if (found && reverse) break;
                  if (!found && !reverse) break;
                  if (!skipvar) *cptr++ = (char)ch;
                  mcnt++;
                  inch();
                  if ((fp != NULL && ch == EOF) || (fp == NULL && ch == 0))
                     break;                                   // if at EOF or end of string, bug out
                  }
               if (mcnt > 0) {
                  if (!skipvar) *cptr++ = '\0';
                  cnt++;
                  }
               else break;
               format = last + 1;
               }
            else if (*format == 'c') {
               if (!skipvar) cptr = va_arg(arg, char *);
               if ((fp != NULL && ch == EOF) || (fp == NULL && ch == 0))
                  fin = 1;                                                // at EOF or end of string
               else {
                  if (!skipvar) *cptr = ch;
                  cnt++;
                  inch();
                  }
               }
            else if (*format == 'n') {
               uptr = va_arg(arg, unsigned int *);
               if (fp != NULL) *uptr = ftell(fp) - startpos;
                 else *uptr = startp - s;
               }
            else if (*format == 'd' || *format == 'u' || *format == 'x' || *format == 'o' ||
               *format == 'p' || *format == 'i') {
               int neg = 0;
               unsigned long x = 0;
               int undecided = 0;
               int base = 10;
               int reallyp = 0;
               int mcnt = 0;

               if (*format == 'x') base = 16;
               else if (*format == 'p') base = 16;
               else if (*format == 'o') base = 8;
               else if (*format == 'i') base = 0;
               if (!skipvar) {
                  if ((*format == 'd') || (*format == 'i')) {
                     if (modlong) lptr = va_arg(arg, long *);
                     else if (modshort) hptr = va_arg(arg, short *);
                     else iptr = va_arg(arg, int *);
                     }
                  else {
                     if (modlong) luptr = va_arg(arg, unsigned long *);
                     else if (modshort) huptr = va_arg(arg, unsigned short *);
                     else uptr = va_arg(arg, unsigned int *);
                     }
                  }
               while (ch>=0 && isspace(ch)) inch();                       // skip leading whitespace
               if (ch == '-') {
                  neg = 1;
                  inch();
                  }
               else if(ch == '+') inch();

                  // This logic is the same as strtoul so if you change this, change that one too.
               if (base == 0) undecided = 1;
               while (!((fp != NULL && ch == EOF) || (fp == NULL && ch == 0))) {
                  if (isdigit((unsigned char)ch)) {
                     if (base == 0) {
                        if (ch == '0') base = 8;
                        else {
                           base = 10;
                            undecided = 0;
                           }
                        }
                     x = x * base + (ch - '0');
                     inch();
                     }
                  else if (isalpha((unsigned char)ch)) {
                     if ((ch == 'X') || (ch == 'x')) {
                        if ((base == 0) || ((base == 8) && undecided)) {
                           base = 16;
                           undecided = 0;
                           inch();
                           }
                        else if (base == 16) inch();           // hex values may have an optional 0x
                        else break;
                        }
                     else if (base <= 10) break;
                     else {
                        x = x * base + (toupper((unsigned char)ch) - 'A') + 10;
                        inch();
                        }
                     }
                  else break;
                  mcnt++;
                  }                                                          // end of strtoul logic

               if (mcnt == 0) break;        // if we didn't get any characters, don't go any further
               if (!skipvar) {
                  if ((*format == 'd') || (*format == 'i')) {
                     int lval;

                     if (neg) lval = (long) - x;
                     else lval = (long)x;
                     if (modlong) *lptr=lval;                          // l modifier: assign to long
                     else if (modshort) *hptr = (short)lval;                           // h modifier
                     else *iptr=(int)lval;
                     }
                  else {
                     if (modlong) *luptr = (unsigned long)x;
                     else if (modshort) *huptr = (unsigned short)x;
                     else *uptr = (unsigned int)x;
                     }
                  }
               cnt++;
               }
            else if (*format=='e' || *format=='f' || *format=='g' || *format=='E' || *format=='G') {
                  // Floating-point (double or float) input item
               int negsw1,negsw2,dotsw,expsw,ndigs1,ndigs2,nfdigs;
               int ntrailzer,expnum,expsignsw;
               double fpval,pow10;

               if (!skipvar) {
                  if (modlong) dptr = va_arg(arg, double *);
                  else fptr = va_arg(arg, float *);
                  }
               negsw1=0;                                                                     // init
               negsw2=0;
               dotsw=0;
               expsw=0;
               ndigs1=0;
               ndigs2=0;
               nfdigs=0;
               ntrailzer=0;                                     // # of trailing 0's unaccounted for
               expnum=0;
               expsignsw=0;                                    // nonzero means done +/- on exponent
               fpval=0.0;
               while (ch>=0 && isspace(ch)) inch();                       // skip leading whitespace
               if (ch == '-') {
                  negsw1=1;
                  inch();
                  }
               else if (ch=='+') inch();
               while (ch>0) {
                  if (ch=='.' && dotsw==0 && expsw==0) dotsw=1;
                  else if (isdigit(ch)) {
                     if (expsw) {
                        ndigs2++;
                        expnum=expnum*10+(ch-'0');
                        }
                     else {
                           // To avoid overflow or loss of precision, skip leading and trailing 0
                           // unless needed. (Automatic for leading 0s, since 0.0*10.0 is 0.0).
                        ndigs1++;
                        if (dotsw) nfdigs++;
                        if (ch=='0' && fpval!=0.) ntrailzer++;                // possible trailing 0
                        else {                                    // account for any preceding zeros
                           while (ntrailzer>0) {
                              fpval*=10.;
                              ntrailzer--;
                              }
                           fpval=fpval*10.0+(ch-'0');
                           }
                        }
                     }
                  else if ((ch=='e' || ch=='E') && expsw==0) expsw=1;
                  else if ((ch=='+' || ch=='-') && expsw==1 && ndigs2==0 && expsignsw==0) {
                     expsignsw=1;
                     if (ch=='-') negsw2=1;
                     }
                  else break;                                         // bad char: end of input item
                  inch();
                  }
               if ((fp != NULL && ch == EOF) || (fp == NULL && ch == 0)) fin=1;
               if (ndigs1==0 || (expsw && ndigs2==0)) return(cnt);    // check for valid fl-pt value
               if (negsw2) expnum=-expnum;                               // complete the fl-pt value
               expnum+=ntrailzer-nfdigs;
               if (expnum!=0 && fpval!=0.0) {
                  negsw2=0;
                  if (expnum<0) {
                     expnum=-expnum;
                     negsw2=1;
                     }
                     // Multiply or divide by 10.0**expnum, using bits of expnum (fast method).
                  pow10=10.0;
                  for (;;) {
                      if (expnum & 1) {                                             // low-order bit
                         if (negsw2) fpval/=pow10;
                         else fpval*=pow10;
                         }
                      expnum>>=1;                                               // shift right 1 bit
                      if (expnum==0) break;
                      pow10*=pow10;                                  // 10.**n where n is power of 2
                      }
                  }
               if (negsw1) fpval=-fpval;
               if (!skipvar) {                                       // l modifier: assign to double
                  if (modlong) *dptr=fpval;
                  else *fptr=(float)fpval;
                  }
               cnt++;
               }
            }

      }
   else if (isspace((unsigned char)(*format))) {
         // Whitespace char in format string: skip next whitespace chars in input data.  This
         // supports input of multiple data items.
      while (ch>=0 && isspace(ch)) inch();
      }
   else {                                               // some other character in the format string
      if (ch != *format) return (cnt);
      inch();
      }
   format++;
   if ((fp != NULL && ch == EOF) || (fp == NULL && ch == 0)) fin = 1;                         // EOF
   }
if (fp != NULL) ungetc(ch, fp);
return (cnt);
}                                                                                  // end of vvscanf
