/*
 * Copyright (c) 1999, 2020 Tanuki Software, Ltd.
 * http://www.tanukisoftware.com
 * All rights reserved.
 *
 * This software is the proprietary information of Tanuki Software.
 * You shall use it only in accordance with the terms of the
 * license agreement you entered into with Tanuki Software.
 * http://wrapper.tanukisoftware.com/doc/english/licenseOverview.html
 */

#ifndef _LOCALIZE
 #define _LOCALIZE
 #include <stdio.h>

 #ifndef WIN32
    
   #define MBSTOWCS_QUERY_LENGTH 0

  #ifdef UNICODE
   #include <wchar.h>
   #ifdef _sntprintf
    #undef _sntprintf
   #endif

   #include <stdarg.h>
   #include <stdlib.h>
   #include <unistd.h>
   #include <sys/types.h>
   #include <sys/stat.h>
   #include <locale.h>
   #include <syslog.h>
   #include <time.h>
   #include <wctype.h>
  
   #define __max(x,y) (((x) > (y)) ? (x) : (y))
   #define __min(x,y) (((x) < (y)) ? (x) : (y))

   #if defined(SOLARIS) || defined(HPUX)
    #define WRAPPER_USE_PUTENV
   #endif

   #ifdef FREEBSD
    #include <dlfcn.h>
   #endif


   #if defined(MACOSX) || defined(HPUX) || defined(FREEBSD) || defined(SOLARIS)
    #ifndef wcscasecmp
extern int wcscasecmp(const wchar_t* s1, const wchar_t* s2);
     #define ECSCASECMP
    #endif
   #endif


   #define TEXT(x) L##x
typedef wchar_t TCHAR;
typedef wchar_t _TUCHAR;

extern int _tprintf(const wchar_t *fmt,...) ;
extern int multiByteToWideChar(const char *multiByteChars, const char *multiByteEncoding, char *interumEncoding, wchar_t **outputBuffer, int localizeErrorMessage);
extern int converterMBToWide(const char *multiByteChars, const char *multiByteEncoding, wchar_t **outputBufferW, int localizeErrorMessage);

#define _taccess      _waccess
#define _tstoi64      _wtoi64
#define _ttoi64       _wtoi64
#define cgetts        _cgetws
extern int _tchdir(const TCHAR *path);
extern int _texecvp(TCHAR* arg, TCHAR **cmd);
extern int _tmkfifo(TCHAR* arg, mode_t mode);
#define _tchmod       _wchmod
extern int _tchown(const TCHAR *path, uid_t owner, gid_t group);
#define _tcprintf     _cwprintf
#define _cputts       _cputws
#define _tcreat       _wcreat
#define _tcscanf      _cwscanf
#define _tctime64     _wctime64
#define _texecl       _wexecl
#define _texecle      _wexecle
#define _texeclp      _wexeclp
#define _texeclpe     _wexeclpe
#define _texecv       _wexecv
extern int _texecve(TCHAR* arg, TCHAR **cmd, TCHAR** env);
#define _texecvpe     _wexecvpe
#define _tfdopen      _wfdopen
#define _fgettchar    _fgetwchar
#define _tfindfirst   _wfindfirst
#define _tfindnext64  _wfindnext64
#define _tfindnext    _wfindnext
#define _tfindnexti64 _wfindnexti64
#define _fputtchar    _fputwchar
#define _tfsopen      _wfsopen
#define _tfullpath    _wfullpath
#define _gettch       _getwch
#define _gettche      _getwche
extern TCHAR* _tgetcwd(TCHAR *buf, size_t size);
#define _tgetdcwd     _wgetdcwd
#define _ltot         _ltow
#define _tmakepath    _wmakepath
#define _tmkdir       _wmkdir
#define _tmktemp      _wmktemp
extern int _topen(const TCHAR *path, int oflag, mode_t mode);
#define _puttch       _putwch
#if defined(WRAPPER_USE_PUTENV)
extern int _tputenv(const TCHAR *string);
#else
extern int _tsetenv(const TCHAR *name, const TCHAR *value, int overwrite);
extern void _tunsetenv(const TCHAR *name);
#endif
#define _trmdir       _wrmdir
#define _sctprintf    _scwprintf
#define _tsearchenv   _wsearchenv

#define _sntscanf     _snwscanf
#define _tsopen       _wsopen
#define _tspawnl      _wspawnl
#define _tspawnle     _wspawnle
#define _tspawnlp     _wspawnlp
#define _tspawnlpe    _wspawnlpe
#define _tspawnv      _wspawnv
#define _tspawnve     _wspawnve
#define _tspawnvp     _wspawnvp
#define _tspawnvpe    _wspawnvpe
#define _tsplitpath   _wsplitpath
#define _tstat64      _wstat64
extern int _tstat(const wchar_t* filename, struct stat *buf);

#define _tstati64     _wstati64
#define _tstrdate     _wstrdate
#define _tcsdec       _wcsdec
#define _tcsdup       wcsdup   /* replaced _wcsdup by wcsdup - but both not supported on Zos */
#define _tcsicmp      wcscasecmp
/* Intentionally do not allow use of _trealpath because it does not specify a buffer length.
 * #define _trealpath
 * Define our own _trealpathN below. */
extern wchar_t* _trealpathN(const wchar_t* fileName, wchar_t *resolvedName, size_t resolvedNameSize);
#define _tcsicoll     _wcsicoll
#define _tcsinc       _wcsinc
#define _tcslwr       _wcslwr
#define _tcsnbcnt     _wcsncnt
#define _tcsnccnt     _wcsncnt
#define _tcsnccnt     _wcsncnt
#define _tcsnccoll    _wcsncoll
#define _tcsnextc     _wcsnextc
#define _tcsncicmp    _wcsnicmp
#define _tcsnicmp     _wcsnicmp
#define _tcsncicoll   _wcsnicoll
#define _tcsnicoll    _wcsnicoll
#define _tcsninc      _wcsninc
#define _tcsncset     _wcsnset
#define _tcsnset      _wcsnset
#define _tcsrev       _wcsrev
#define _tcsset       _wcsset
#define _tcsspnp      _wcsspnp
#define _tstrtime     wcsftime
#define _tcstoi64     _wcstoi64
#define _tcstoui64    _wcstoui64
#define _tcsupr       _wcsupr
#define _ttempnam     _wtempnam
#define _ui64tot      _ui64tow
#define _ultot        _ultow
#define _ungettch     _ungetwch
extern int _tunlink(const wchar_t* address);
#define _tutime64     _wutime64
#define _tutime       _wutime
#define _vsctprintf   _vscwprintf
#if defined(HPUX)
extern int _vsntprintf(wchar_t *ws, size_t n, const wchar_t *format, va_list arg);
#else
#define _vsntprintf   vswprintf
#endif
#define _tasctime     _wasctime
#define _tstof        _wtof
#define _tstoi        _wtoi
#define _ttoi(x)      wcstol(x, NULL, 10)
#define _tstol        _wtol
#define _ttol         _wtol
#define _tctime       _wctime
#define _fgettc       fgetwc
#define _fgetts       fgetws
extern FILE* _tfopen(const wchar_t* file, const wchar_t* mode);
extern FILE* _tpopen(const wchar_t* command, const wchar_t* mode);
#define _fputtc       fputwc
#define _fputts       fputws
#define _tfreopen     _wfreopen
#define _ftscanf      fwscanf
#define _gettc        getwc
#define _gettchar     getwchar
/**
 * This Wrapper function internally does a malloc to generate the
 *  Wide-char version of the return string.  This must be freed by the caller.
 *  Only needed inside the following:
 *  #if !defined(WIN32) && defined(UNICODE)
 *  #endif
 */
extern TCHAR * _tgetenv ( const TCHAR * name );
#define _getts        getws
#define _istalnum     iswalnum
#define _istalpha     iswalpha
#define _istascii     iswascii
#define _istcntrl     iswcntrl
#define _istdigit     iswdigit
#define _istgraph     iswgraph
#define _istleadbyte  isleadbyte
#define _istlower     iswlower
#define _istprint     iswprint
#define _istpunct     iswpunct
#define _istspace     iswspace
#define _istupper     iswupper
#define _istxdigit    iswxdigit
#define _tmain        wmain
#define _tperror      _wperror
/*_tprintf  wprintf*/
#define _puttc        putwc
#define _puttchar     putwchar
#define _putts        _putws
extern int _tremove(const TCHAR *path);
extern int _trename(const TCHAR *path, const TCHAR *to);
extern void _topenlog(const TCHAR *ident, int logopt, int facility);
extern void _tsyslog(int priority, const TCHAR *message);
#define _tscanf       wscanf
extern TCHAR *_tsetlocale(int category, const TCHAR *locale) ;
extern int _sntprintf(TCHAR *str, size_t size, const TCHAR *format, ...);
#define _stprintf     _sntprintf
extern int _ftprintf(FILE *stream, const wchar_t *format, ...);
#define _stscanf      swscanf
#define _tcscat       wcscat
#define _tcschr       wcschr
#define _tcscmp       wcscmp
#define _tcscoll      wcscoll
#define _tcscpy       wcscpy
#define _tcscspn      wcscspn
#define _tcserror     _wcserror
#define _tcsftime     wcsftime
#define _tcsclen      wcslen
#define _tcslen       wcslen
#define _tcsncat      wcsncat
#define _tcsnccat     wcsncat
#define _tcsnccmp     wcsncmp
#define _tcsncmp      wcsncmp
#define _tcsnccpy     wcsncpy
#define _tcsncpy      wcsncpy
#define _tcspbrk      wcspbrk
#define _tcsrchr      wcsrchr
#define _tcsspn       wcsspn
#define _tcsstr       wcsstr
#define _tcstod       wcstod
#define _tcstok       wcstok
#define _tcstol       wcstol
#define _tcstoul      wcstoul
#define _tcsxfrm      wcsxfrm
#define _tsystem      _wsystem
#define _ttmpnam      _wtmpnam
#define _totlower     towlower
#define _totupper     towupper
#define _ungettc      ungetwc
#define _vftprintf    vfwprintf
#define _vtprintf     vwprintf
#define _vstprintf    vswprintf
extern size_t _treadlink(TCHAR* exe, TCHAR* fullpath, size_t size);

extern long _tpathconf(const TCHAR *path, int name);

#else /* ASCII */

#define TEXT(x) x
typedef char TCHAR;
typedef unsigned char _TUCHAR;
#define _tpathconf    pathconf
#define _taccess      _access
#define _treadlink    readlink
#define _tstoi64      _atoi64
#define _ttoi64       _atoi64
#define cgetts        _cgets
#define _tchdir       chdir
#define _tchmod       chmod
#define _tcprintf     _cprintf
#define _cputts       _cputs
#define _tcreat       _creat
#define _tcscanf      _cscanf
#define _tctime64     _ctime64
#define _tmkfifo      mkfifo
#define _texecl       execl
#define _texecle      execle
#define _texeclp      execlp
#define _texeclpe     execlpe
#define _texecv       execv
#define _texecve      execve
#define _texecvp      execvp
#define _texecvpe     execvpe
#define _tfdopen      _fdopen
#define _fgettchar    _fgetchar
#define _tfindfirst   _findfirst
#define _tfindnext64  _findnext64
#define _tfindnext    _findnext
#define _tfindnexti64 _findnexti64
#define _fputtchar    _fputchar
#define _tfsopen      _fsopen
#define _tfullpath    _fullpath
#define _gettch       _getch
#define _gettche      _getche
#define _tgetcwd      getcwd
#define _tgetdcwd     getdcwd
#define _ltot         _ltoa
#define _tmakepath    _makepath
#define _tmkdir       _mkdir
#define _tmktemp      _mktemp
#define _topen        open
#define _puttch       _putch
/*#define _tputenv      putenv*/
#define _tsetenv      setenv
#define _tunsetenv      unsetenv
#define _trmdir       _rmdir
#define _sctprintf    _scprintf
#define _tsearchenv   _searchenv
#define _sntprintf    _snprintf
#define _sntscanf     _snscanf
#define _tsopen       _sopen
#define _tspawnl      _spawnl
#define _tspawnle     _spawnle
#define _tspawnlp     _spawnlp
#define _tspawnlpe    _spawnlpe
#define _tspawnv      _spawnv
#define _tspawnve     _spawnve
#define _tspawnvp     _spawnvp
#define _tspawnvpe    _spawnvpe
#define _tsplitpath   _splitpath
#define _tstat64      _stat64
#define _tstat        stat
#define _tstati64     _stati64
#define _tstrdate     _strdate
#define _tcsdec       _strdec
#define _tcsdup       strdup    /* replaced _strdup by strdup */
#define _tcsicmp      strcasecmp
#define _tcsicoll     _stricoll
#define _tcsinc       _strinc
/* Intentionally do not allow use of _trealpath because it does not specify a buffer length.
 * #define _trealpath    realpath
 * Define our own _trealpathN below. */
#define _trealpathN(fileName, resolvedName, resolvedNameSize)    realpath(fileName, resolvedName)
#define _tcslwr       _strlwr
#define _tcsnbcnt     _strncnt
#define _tcsnccnt     _strncnt
#define _tcsnccnt     _strncnt
#define _tcsnccoll    _strncoll
#define _tcsnextc     _strnextc
#define _tcsncicmp    _strnicmp
#define _tcsnicmp     _strnicmp
#define _tcsncicoll   _strnicoll
#define _tcsnicoll    _strnicoll
#define _tcsninc      _strninc
#define _tcsncset     _strnset
#define _tcsnset      _strnset
#define _tcsrev       _strrev
#define _tcsset       _strset
#define _tcsspnp      _strspnp
#define _tstrtime     strftime
#define _tcstoi64     _strtoi64
#define _tcstoui64    _strtoui64
#define _tcsupr       _strupr
#define _ttempnam     _tempnam
#define _ui64tot      _ui64toa
#define _ultot        _ultoa
#define _ungettch     _ungetch
#define _tunlink      unlink
#define _tutime64     _utime64
#define _tutime       _utime
#define _vsctprintf   _vscprintf
#define _vsntprintf   vsnprintf
#define _tasctime     asctime
#define _tstof        atof
#define _tstoi        atoi
#define _ttoi         atoi
#define _tstol        atol
#define _ttol         atol
#define _tctime       ctime
#define _fgettc       fgetc
#define _fgetts       fgets
#define _tfopen       fopen
#define _tpopen       popen
#define _ftprintf     fprintf
#define _fputtc       fputc
#define _fputts       fputs
#define _tfreopen     freopen
#define _ftscanf      fscanf
#define _gettc        getc
#define _gettchar     getchar
#define _tgetenv      getenv
#define _getts        gets
#define _istalnum     isalnum
#define _istalpha     isalpha
#define _istascii     isascii
#define _istcntrl     iscntrl
#define _istdigit     isdigit
#define _istgraph     isgraph
#define _istlead      islead
#define _istleadbyte  isleadbyte
#define _istlegal     islegal
#define _istlower     islower
#define _istprint     isprint
#define _istpunct     ispunct
#define _istspace     isspace
#define _istupper     isupper
#define _istxdigit    isxdigit
#define _tmain        main
#define _tperror      perror
#define _tprintf      printf
#define _puttc        putc
#define _puttchar     putchar
#define _putts        puts
#define _tremove      remove
#define _trename      rename
#define _tscanf       scanf
#define _tsetlocale   setlocale
#define _sntprintf    snprintf
#define _stscanf      sscanf
#define _tcscat       strcat
#define _tcschr       strchr
#define _tcscmp       strcmp
#define _tcscoll      strcoll
#define _tcscpy       strcpy
#define _tcscspn      strcspn
#define _tcserror     strerror
#define _tcsftime     strftime
#define _tcsclen      strlen
#define _tcslen       strlen
#define _tcsncat      strncat
#define _tcsnccat     strncat
#define _tcsnccmp     strncmp
#define _tcsncmp      strncmp
#define _tcsnccpy     strncpy
#define _tcsncpy      strncpy
#define _tcspbrk      strpbrk
#define _tcsrchr      strrchr
#define _tcsspn       strspn
#define _tcsstr       strstr
#define _tcstod       strtod
#define _tcstok       strtok
#define _tcstol       strtol
#define _tcstoul      strtoul
#define _tcsxfrm      strxfrm
#define _tsystem      system
#define _ttmpnam      tmpnam
#define _totlower     tolower
#define _totupper     toupper
#define _ungettc      ungetc
#define _vftprintf    vfprintf
#define _vtprintf     vprintf
#define _vstprintf    vsprintf
#define _topenlog     openlog
#define _tsyslog      syslog
#endif
#else /* WIN32 */
#include <tchar.h>
#include <sys/types.h>
#include <sys/stat.h>
extern int multiByteToWideChar(const char *multiByteChars, int encoding, TCHAR **outputBufferW, int localizeErrorMessage);
#endif
/* Define boolean constants. */
#ifndef TRUE
 #define TRUE -1
#endif

#ifndef FALSE
 #define FALSE 0
#endif

#ifndef WIN32
 #ifdef HPUX
  #define MB_UTF8   "utf8"
 #else
  #define MB_UTF8   "UTF-8"
 #endif
 #define __UTF8     MB_UTF8
#else
 #define __UTF8     CP_UTF8
#endif

#define TCHAR_TAB   TEXT('\t')

#ifdef WIN32
 #define FILE_SEPARATOR     TEXT("\\")
 #define FILE_SEPARATOR_C   TEXT('\\')
#else
 #define FILE_SEPARATOR     TEXT("/")
 #define FILE_SEPARATOR_C   TEXT('/')
#endif

#define ENCODING_BUFFER_SIZE    32                                              /* ex: x-windows-iso2022jp */

void wrapperSecureZero(void* str, size_t size);

void wrapperSecureFree(void* str, size_t size);

void wrapperSecureFreeStrW(TCHAR* str);

void wrapperSecureFreeStrMB(char* str);

extern TCHAR* toLower(const TCHAR* value);

extern TCHAR* toUpper(const TCHAR* value);

extern void clearNonAlphanumeric(TCHAR* bufferIn, TCHAR* bufferOut);

extern int compareEncodings(TCHAR* encoding1, TCHAR* encoding2, int ignoreCase, int ignorePunctuation);

extern int compareEncodingsSysMode(TCHAR* encoding1, TCHAR* encoding2);

#ifndef WIN32
/**
 * Get the encoding of the current locale.
 *
 * @param buffer output buffer
 *
 * @return the buffer or NULL if the encoding could not be retrieved.
 */
TCHAR* getCurrentLocaleEncoding(TCHAR* buffer);
#endif

#ifndef WIN32
 #define ICONV_ENCODING_SUPPORTED     0
 #define ICONV_ENCODING_KNOWN_ISSUE   1
 #define ICONV_ENCODING_NOT_SUPPORTED 2

/**
 * Check if the given encoding is supported by the iconv library.
 *
 * @return ICONV_ENCODING_SUPPORTED if the encoding is supported,
 *         ICONV_ENCODING_KNOWN_ISSUE if the encoding exist on iconv but fails to convert some characters.
 *         ICONV_ENCODING_NOT_SUPPORTED if the encoding is not supported.
 */
int getIconvEncodingMBSupport(const char* encodingMB);
/**
 * Check if the given encoding is supported by the iconv library.
 *
 * @return ICONV_ENCODING_SUPPORTED if the encoding is supported,
 *         ICONV_ENCODING_KNOWN_ISSUE if the encoding exist on iconv but fails to convert some characters.
 *         ICONV_ENCODING_NOT_SUPPORTED if the encoding is not supported.
 */
int getIconvEncodingSupport(const TCHAR* encoding);
#endif

#ifdef _LIBICONV_VERSION
 #ifdef AIX /* the AIX version of iconv.h doesn't have _LIBICONV_VERSION (need to check the other platforms) */
  #define USE_LIBICONV_GNU
 #endif
#endif

#ifdef FREEBSD
/**
 * Get the name of the iconv library that was loaded.
 *
 * @return the name of the iconv library (wide chars).
 */
extern TCHAR* getIconvLibName();

/**
 * Tries to load libiconv and then fallback in FreeBSD.
 * Unfortunately we can not do any pretty logging here as iconv is
 *  required for all of that to work.
 *
 * @return TRUE if there were any problems, FALSE otherwise.
 */
extern int loadIconvLibrary();
#endif

/**
 * Define a cross platform way to compare strings while ignoring case.
 */
#ifdef WIN32
#define strIgnoreCaseCmp _stricmp
#else
#define strIgnoreCaseCmp strcasecmp
#endif
#define strcmpIgnoreCase(str1, str2) _tcsicmp(str1, str2)

/**
 * Function to get the system encoding name/number for the encoding
 * of the conf file
 *
 * @para String holding the encoding from the conf file
 *
 * @return TRUE if not found, FALSE otherwise
 *
 */
#ifdef WIN32
extern int getEncodingByName(char* encodingMB, int *encoding);
#else
extern int getEncodingByName(char* encodingMB, char** encoding);
#endif

#ifdef WIN32
/**
 * Converts a Wide string into a specific multibyte encoded string.
 *
 * @prarm wideChars The Wide string to be converted.
 * @param outputBufferMB Returns a newly malloced buffer containing the target MB chars.
 *                       Will contain an error message if the function returns TRUE.
 *                       If this is NULL then there was an out of memory problem.
 *                       Caller must free this up.
 * @param outputEncoding Output encoding to use.
 *
 * @return TRUE if there were any problems.  False otherwise.
 */
extern int converterWideToMB(const TCHAR *wideChars, char **outputBufferMB, int outputEncoding);

/**
 * Converts a native multibyte string into a specific multibyte encoded string.
 *
 * @param multiByteChars The original multi-byte chars.
 * @param inputEncoding The multi-byte encoding.
 * @param outputBufferMB Returns a newly malloced buffer containing the target MB chars.
 *                       Will contain an error message if the function returns TRUE.
 *                       If this is NULL then there was an out of memory problem.
 *                       Caller must free this up.
 * @param outputEncoding Output encoding to use.
 *
 * @return -1 if there were any problems.  buffer size (>=0) if everything was Ok.
 */
extern int converterMBToMB(const char *multiByteChars, int inputEncoding, char **outputBufferMB, int outputEncoding);
#else 

#ifdef HPUX
/**
 * Turns on or off a fix used in converterMBToMB()
 */
void toggleIconvHpuxFix(int value);
#endif

/**
 * Converts a native multibyte string into a specific multibyte encoded string.
 *
 * @param multiByteChars The original multi-byte chars.
 * @param multiByteEncoding The multi-byte encoding.
 * @param outputBufferMB Returns a newly malloced buffer containing the target MB chars.
 *                       Will contain an error message if the function returns TRUE.
 *                       If this is NULL then there was an out of memory problem.
 *                       Caller must free this up.
 * @param outputEncoding Output encoding to use.
 *
 * @return -1 if there were any problems.  buffer size (>=0) if everything was Ok.
 */
extern int converterMBToMB(const char *multiByteChars, const char *multiByteEncoding, char **outputBufferMB, const char *outputEncoding);

/**
 * Converts a Wide string into a specific multibyte encoded string.
 *
 * @prarm wideChars The Wide string to be converted.
 * @param outputBufferMB Returns a newly malloced buffer containing the target MB chars.
 *                       Will contain an error message if the function returns TRUE.
 *                       If this is NULL then there was an out of memory problem.
 *                       Caller must free this up.
 * @param outputEncoding Output encoding to use.
 *
 * @return TRUE if there were any problems.  False otherwise.
 */
extern int converterWideToMB(const TCHAR *wideChars, char **outputBufferMB, const char *outputEncoding);
#endif

/**
 * Gets the error code for the last operation that failed.
 */
extern int wrapperGetLastError();

/*
 * Corrects a path in place by replacing all '/' characters with '\'
 *  on Windows platforms.  Does nothing on NIX platforms.
 *
 * filename - Filename to be modified.  Could be null.
 */
extern int wrapperCorrectWindowsPath(TCHAR *filename);

/*
 * Corrects a path in place by replacing all '\' characters with '/'
 *  on NIX platforms.  Does nothing on Windows platforms.
 *
 * filename - Filename to be modified.  Could be null.
 */
extern int wrapperCorrectNixPath(TCHAR *filename);
#endif


/* Helper defines used to help trace where certain calls are being made. */
/*#define DEBUG_MBSTOWCS*/
#ifdef DEBUG_MBSTOWCS
 #ifdef WIN32
  #define mbstowcs(x,y,z) mbstowcs(x,y,z); wprintf(L"%S:%d:%S mbstowcs(%S, %S, %S) -> mbstowcs(%p, \"%S\", %d)\n", __FILE__, __LINE__, __FUNCTION__, #x, #y, #z, (void *)x, y, (int)z)
 #else
  #define mbstowcs(x,y,z) mbstowcs(x,y,z); wprintf(L"%s:%d:%s mbstowcs(%s, %s, %s) -> mbstowcs(%p, \"%s\", %d)\n", __FILE__, __LINE__, __FUNCTION__, #x, #y, #z, (void *)x, y, (int)z)
 #endif
#endif

/*#define DEBUG_MALLOC*/
#ifdef DEBUG_MALLOC
 extern void *malloc2(size_t size, const char *file, int line, const char *func, const char *sizeVar);
 #define malloc(x) malloc2(x, __FILE__, __LINE__, __FUNCTION__, #x)
 #ifdef WIN32
  #define free(x) wprintf(L"%S:%d:%S free(%S) -> free(%p)\n", __FILE__, __LINE__, __FUNCTION__, #x, (void *)x); free(x)
 #else
  #define free(x) wprintf(L"%s:%d:%s free(%s) -> free(%p)\n", __FILE__, __LINE__, __FUNCTION__, #x, (void *)x); free(x)
 #endif
#endif

