/*
 * This file or a portion of this file is licensed under the terms of
 * the Globus Toolkit Public License, found in file GTPL, or at
 * http://www.globus.org/toolkit/download/license.html. This notice must
 * appear in redistributions of this file, with or without modification.
 *
 * Redistributions of this Software, with or without modification, must
 * reproduce the GTPL in: (1) the Software, or (2) the Documentation or
 * some other similar material which is provided with the Software (if
 * any).
 *
 * Copyright 1999-2004 University of Chicago and The University of
 * Southern California. All rights reserved.
 */
#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

#include "rwio.h"
#include "debug.h"
#include "appinfo.h"
#include "mysystem.h"
#include "mylist.h"
#include "invoke.h"
#include "tools.h"
#include "version.h"
#include "ptrace.h"

/* truely shared globals */
int isExtended = 1;     /* timestamp format concise or extended */
int isLocal = 1;        /* timestamp time zone, UTC or local */
extern int make_application_executable;
extern size_t data_section_size;

/* module local globals */
static int doFlush = 0; /* apply fsync() on kickstart's stdout if true */ 
static AppInfo appinfo; /* sigh, needs to be global for atexit handler */
static volatile sig_atomic_t global_no_atexit;

static
int
obtainStatusCode( int raw ) 
/* purpose: convert the raw result from wait() into a status code
 * paramtr: raw (IN): the raw exit code
 * returns: a cooked exit code */
{
  int result = 127;

  if ( raw < 0 ) {
    /* nothing to do to result */
  } else if ( WIFEXITED(raw) ) {
    result = WEXITSTATUS(raw);
  } else if ( WIFSIGNALED(raw) ) {
    result = 128 + WTERMSIG(raw); 
  } else if ( WIFSTOPPED(raw) ) {
    /* nothing to do to result */
  }

  return result;
}

static
int
prepareSideJob( JobInfo* scripting, const char* value )
/* purpose: prepare a side job from environment string
 * paramtr: scripting (OUT): side job info structure
 *          value (IN): value of the environment setting
 * returns: 0 if there is no job to execute
 *          1 if there is a job to run thru mysystem next
 */
{
  /* no value, no job */
  if ( value == NULL ) return 0;

  /* set-up scripting structure (which is part of the appinfo) */
  initJobInfoFromString( scripting, value );

  /* execute process, if there is any */
  if ( scripting->isValid != 1 ) return 0;

  return 1;
}

StatInfo*
initStatFromList( mylist_p list, size_t* size )
/* purpose: Initialize the statlist and statlist size in appinfo.
 * paramtr: list (IN): list of filenames
 *          size (OUT): statlist size to be set
 * returns: a vector of initialized statinfo records, or NULL
 */
{
  StatInfo* result = NULL;

  if ( (*size = list->count) ) {
    size_t i = 0;
    mylist_item_p item = list->head;

    if ( (result = (StatInfo*) calloc( sizeof(StatInfo), *size )) ) {
      while ( item && i < *size ) {
        initStatInfoFromName( result+i, item->pfn, O_RDONLY, 0 );
        if ( item->lfn != NULL ) addLFNToStatInfo( result+i, item->lfn );
        item = item->next;
        ++i;
      }
    }
  }

  return result;
}

#define show( s ) ( s ? s : "(undefined)" )

static
const char*
xlate( const StatInfo* info )
/* purpose: small helper for helpMe() function.
 * paramtr: info (IN): is a record about a file
 * returns: a pointer to the filename, or a local or static buffer w/ info
 * warning: Returns static buffer pointer
 */
{
  static char buffer[16];

  switch ( info->source ) {
  case IS_HANDLE:
    snprintf( buffer, sizeof(buffer), "&%d", info->file.descriptor );
    return buffer;
  case IS_FIFO:
  case IS_TEMP:
  case IS_FILE:
    return show(info->file.name);
  default:
    return "[INVALID]";
  }
}

static
void
helpMe( const AppInfo* run )
/* purpose: print invocation quick help with currently set parameters and
 *          exit with error condition.
 * paramtr: run (IN): constitutes the set of currently set parameters. */
{
  const char* p = strrchr( run->argv[0], '/' );
  if ( p ) ++p;
  else p=run->argv[0];

  fprintf( stderr, 
"Usage:\t%s [-i fn] [-o fn] [-e fn] [-l fn] [-n xid] [-N did] \\\n"
"\t[-w|-W cwd] [-R res] [-s [l=]p] [-S [l=]p] [-X] [-H] [-L lbl -T iso] \\\n" 
"\t[-B sz] [-F] [-f] (-I fn | app [appflags])\n", p );
  fprintf( stderr, 
" -i fn\tConnects stdin of app to file fn, default is \"%s\".\n", 
           xlate(&run->input) );
  fprintf( stderr, 
" -o fn\tConnects stdout of app to file fn, default is \"%s\".\n",
           xlate(&run->output) );
  fprintf( stderr, 
" -e fn\tConnects stderr of app to file fn, default is \"%s\".\n", 
           xlate(&run->error) );
  fprintf( stderr, 
" -l fn\tProtocols invocation record into file fn, default is \"%s\".\n",
           xlate(&run->logfile) );

  fprintf( stderr, 
" -n xid\tProvides the TR name, default is \"%s\".\n"
" -N did\tProvides the DV name, default is \"%s\".\n" 
" -R res\tReflects the resource handle into record, default is \"%s\".\n"
" -B sz\tResizes the data section size for stdio capture, default is %zu.\n",
           show(run->xformation), show(run->derivation), 
           show(run->sitehandle), data_section_size );
  fprintf( stderr,
" -L lbl\tReflects the workflow label into record, no default.\n"
" -T iso\tReflects the workflow time stamp into record, no default.\n"
" -H\tOmit <?xml ...?> header and <machine> from record. This is used\n"
"   \tin clustered jobs to supress duplicate information.\n"
" -I fn\tReads job and args from the file fn, one arg per line.\n"
" -V\tDisplays the version and exit.\n"
" -X\tMakes the application executable, no matter what.\n"
" -w dir\tSets a different working directory dir for jobs.\n" 
" -W dir\tLike -w, but also creates the directory dir if necessary.\n"
" -S l=p\tProvides filename pairs to stat after start, multi-option.\n"
" \tIf the arg is prefixed with '@', it is a list-of-filenames file.\n"
" -s l=p\tProvides filename pairs to stat before exit, multi-option.\n"
" \tIf the arg is prefixed with '@', it is a list-of-filenames file.\n"
" -F\tAttempt to fsync kickstart's stdout at exit (should not be necessary).\n"
" -f\tPrint full information including <resource>, <environment> and \n"
"   \t<statcall>. If the job fails, then -f is implied.\n"
" -q\tOmit <data> for <statcall> (stdout, stderr) if the job succeeds.\n"
#ifdef HAS_PTRACE
" -t\tEnable resource usage tracing\n"
#endif
 );

  /* avoid printing of results in exit handler */
  ((AppInfo*) run)->isPrinted = 1;

  /* exit with error condition */
  exit(127);
}

static
void
finish( void )
{
  if ( ! global_no_atexit ) {
    /* log the output here in case of abnormal termination */
    if ( ! appinfo.isPrinted ) { 
      printAppInfo( &appinfo );
    }
    deleteAppInfo( &appinfo );
  }

  /* PM-466 debugging */
  if ( doFlush ) {
    int status = fsync(STDOUT_FILENO);
    if (status != 0) {
      debugmsg("WARNING: fsync(%d)=%d (errno=%d, strerror=%s)\n",
               STDOUT_FILENO, status, errno, strerror(errno));
    }
  }

  nfs_sync( STDERR_FILENO, DEFAULT_SYNC_IDLE );
}

#ifdef DEBUG_ARGV
static
void
show_args( const char* prefix, char** argv, int argc )
{
  int i;
  debugmsg( "argc=%d\n", argc );
  for ( i=0; i<argc; ++i )
    debugmsg( "%s%2d: %s\n", (prefix ? prefix : ""), i, 
              (argv[i] ? argv[i] : "(null)" ) );
}
#endif

static
int
readFromFile( const char* fn, char*** argv, int* argc, int* i, int j )
{
  size_t newc = 2;
  size_t index = 0;
  char** newv = calloc( sizeof(char*), newc+1 );
  if ( expand_arg( fn, &newv, &index, &newc, 0 ) == 0 ) {
#if 0
    /* insert newv into argv at position i */
    char** result = calloc( sizeof(char*), j + index + 1 );
    memcpy( result, *argv, sizeof(char*) * j );
    memcpy( result+j, newv, sizeof(char*) * index );
    *argv = result;
    *argc = j + index;
    *i = j-1;
#else
    /* replace argv with newv */
    *argv = newv;
    *argc = index;
    *i = -1;
#endif

#ifdef DEBUG_ARGV
    show_args( "result", *argv, *argc );
#endif

    return 0;
  } else {
    /* error parsing */
    return -1;
  }
}

static
void
handleOutputStream( StatInfo* stream, const char* temp, int std_fileno )
/* purpose: Initialize stdout or stderr from commandline arguments
 * paramtr: stream (IO): pointer to the statinfo record for stdout or stderr
 *          temp (IN): command-line argument
 *          std_fileno (IN): STD(OUT|ERR)_FILENO matching to the stream
 */
{
  if ( temp[0] == '-' && temp[1] == '\0' ) {
    initStatInfoFromHandle( stream, std_fileno );
  } else if ( temp[0] == '!' ) {
    if ( temp[1] == '^' ) {
      initStatInfoFromName( stream, temp+2, O_WRONLY | O_CREAT | O_APPEND, 6 );
    } else {
      initStatInfoFromName( stream, temp+1, O_WRONLY | O_CREAT | O_APPEND, 2 );
    }
  } else if ( temp[0] == '^' ) {
    if ( temp[1] == '!' ) {
      initStatInfoFromName( stream, temp+2, O_WRONLY | O_CREAT | O_APPEND, 6 );
    } else {
      initStatInfoFromName( stream, temp+1, O_WRONLY | O_CREAT, 7 );
    }
  } else {
    initStatInfoFromName( stream, temp, O_WRONLY | O_CREAT, 3 );
  }
}


extern char** environ;

static
char*
noquote( char* s )
{
  size_t len;

  /* sanity check */
  if ( ! s ) return NULL;
  else if ( ! *s ) return s;
  else len = strlen(s);

  if ( ( s[0] == '\'' && s[len-1] == '\'' ) || 
       ( s[0] == '"' && s[len-1] == '"' ) ) {
    char* tmp = calloc( sizeof(char), len );
    memcpy( tmp, s+1, len-2 );
    return tmp;
  } else {
    return s;
  }
}

int
main( int argc, char* argv[] )
{
  size_t m, cwd_size = getpagesize();
  int status, result;
  int i, j, keeploop;
  int createDir = 0;
  const char* temp;
  const char* workdir = NULL;
  mylist_t initial;
  mylist_t final;
 
  /* premature init with defaults */
  if ( mylist_init( &initial ) ) return 43;
  if ( mylist_init( &final ) ) return 43;
  initAppInfo( &appinfo, argc, argv );

  /* register emergency exit handler */
  if ( atexit( finish ) == -1 ) {
    appinfo.application.status = -1;
    appinfo.application.saverr = errno;
    fputs( "unable to register an exit handler\n", stderr );
    return 127;
  } else {
    global_no_atexit = 0;
  }

  /* no arguments whatsoever, print help and exit */
  if ( argc == 1 ) helpMe( &appinfo );

  /*
   * read commandline arguments
   * DO NOT use getopt to avoid cluttering flags to the application 
   */
  for ( keeploop=i=1; i < argc && argv[i][0] == '-' && keeploop; ++i ) {
    j = i;
    switch ( argv[i][1] ) {
    case 'B':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -B argument missing\n");
          return 127;
      }
      temp = argv[i][2] ? &argv[i][2] : argv[++i];
      m = strtoul( temp, 0, 0 );
      /* limit max <data> size to 64 MB for each. */
      if ( m < 67108863ul ) data_section_size = m;
      break;
    case 'e':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -e argument missing\n");
          return 127;
      }
      if ( appinfo.error.source != IS_INVALID )
        deleteStatInfo( &appinfo.error );
      temp = ( argv[i][2] ? &argv[i][2] : argv[++i] );
      handleOutputStream( &appinfo.error, temp, STDERR_FILENO );
      break;
    case 'h':
    case '?':
      helpMe( &appinfo );
      break; /* unreachable */
    case 'V':
      puts( KICKSTART_VERSION );
      appinfo.isPrinted=1;
      return 0;
    case 'i':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -i argument missing\n");
          return 127;
      }
      if ( appinfo.input.source != IS_INVALID )
        deleteStatInfo( &appinfo.input );
      temp = argv[i][2] ? &argv[i][2] : argv[++i];
      if ( temp[0] == '-' && temp[1] == '\0' )
        initStatInfoFromHandle( &appinfo.input, STDIN_FILENO );
      else
        initStatInfoFromName( &appinfo.input, temp, O_RDONLY, 2 );
      break;
    case 'H':
      appinfo.noHeader++;
      break;
    case 'f':
      appinfo.fullInfo++;
      break;
    case 'F':
      doFlush++;
      break;
    case 'I':
      /* XXX We expect exactly 1 argument after -I. If we see more,
       * then it is considered to be an error. This should be fixed
       * to work in a more sensible fashion.
       */
      if (argc > i+2) {
          debugmsg("ERROR: No arguments allowed after -I fn\n");
          return 127;
      }

      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -I argument missing\n");
          return 127;
      }

      /* invoke application and args from given file */
      temp = argv[i][2] ? &argv[i][2] : argv[++i];
      if ( readFromFile( temp, &argv, &argc, &i, j ) == -1 ) {
        int saverr = errno;
        debugmsg( "ERROR: While parsing -I %s: %d: %s\n",
                  temp, errno, strerror(saverr) );
        appinfo.application.prefix = strerror(saverr);
        appinfo.application.status = -1;
        return 127;
      }
      keeploop = 0;
      break;
    case 'l':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -l argument missing\n");
          return 127;
      }
      if ( appinfo.logfile.source != IS_INVALID )
        deleteStatInfo( &appinfo.logfile );
      temp = argv[i][2] ? &argv[i][2] : argv[++i];
      if ( temp[0] == '-' && temp[1] == '\0' )
        initStatInfoFromHandle( &appinfo.logfile, STDOUT_FILENO );
      else
        initStatInfoFromName( &appinfo.logfile, temp, O_WRONLY | O_CREAT | O_APPEND, 2 );
      break;
    case 'L':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -L argument missing\n");
          return 127;
      }
      appinfo.wf_label = noquote( argv[i][2] ? &argv[i][2] : argv[++i] );
      break;
    case 'n':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -n argument missing\n");
          return 127;
      }
      appinfo.xformation = noquote( argv[i][2] ? &argv[i][2] : argv[++i] );
      break;
    case 'N':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -N argument missing\n");
          return 127;
      }
      appinfo.derivation = noquote( argv[i][2] ? &argv[i][2] : argv[++i] );
      break;
    case 'o':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -o argument missing\n");
          return 127;
      }
      if ( appinfo.output.source != IS_INVALID )
        deleteStatInfo( &appinfo.output );
      temp = ( argv[i][2] ? &argv[i][2] : argv[++i] );
      handleOutputStream( &appinfo.output, temp, STDOUT_FILENO );
      break;
    case 'q':
      appinfo.omitData = 1;
      break;
    case 'R':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -R argument missing\n");
          return 127;
      }
      appinfo.sitehandle = noquote( argv[i][2] ? &argv[i][2] : argv[++i] );
      break;
    case 'S':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -S argument missing\n");
          return 127;
      }
      temp = argv[i][2] ? &argv[i][2] : argv[++i];
      if ( temp[0] == '@' ) {
        /* list-of-filenames file */
        if ( (result=mylist_fill( &initial, temp+1 )) )
          debugmsg( "ERROR: initial %s: %d: %s\n", 
                    temp+1, result, strerror(result) );
      } else {
        /* direct filename */
        if ( (result=mylist_add( &initial, temp )) )
          debugmsg( "ERROR: initial %s: %d: %s\n", 
                    temp, result, strerror(result) );
      }
      break;
    case 's':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -s argument missing\n");
          return 127;
      }
      temp = argv[i][2] ? &argv[i][2] : argv[++i];
      if ( temp[0] == '@' ) {
        /* list-of-filenames file */
        if ( (result=mylist_fill( &final, temp+1 )) )
          debugmsg( "ERROR: final %s: %d: %s\n", 
                   temp+1, result, strerror(result) );
      } else {
        /* direct filename */
        if ( (result=mylist_add( &final, temp )) )
          debugmsg( "ERROR: final %s: %d: %s\n", 
                   temp, result, strerror(result) );
      }
      break;
    case 'T':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -T argument missing\n");
          return 127;
      }
      appinfo.wf_stamp = noquote( argv[i][2] ? &argv[i][2] : argv[++i] );
      break;
    case 't':
      appinfo.enableTracing++;
      break;
    case 'w':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -w argument missing\n");
          return 127;
      }
      workdir = noquote( argv[i][2] ? &argv[i][2] : argv[++i] );
      createDir = 0;
      break;
    case 'W':
      if (!argv[i][2] && argc <= i+1) {
          debugmsg( "ERROR: -W argument missing\n");
          return 127;
      }
      workdir = noquote( argv[i][2] ? &argv[i][2] : argv[++i] );
      createDir = 1;
      break;
    case 'X':
      make_application_executable++;
      break;
    case '-':
      keeploop = 0;
      break;
    default:
      i -= 1;
      keeploop = 0;
      break;
    }
  }

  /* initialize app info and register CLI parameters with it */
  if ( argc-i > 0 ) {
    /* there is an application to run */
    initJobInfo( &appinfo.application, argc-i, argv+i );
    
    /* is there really something to run? */
    if ( appinfo.application.isValid != 1 ) {
      appinfo.application.status = -1;
      switch ( appinfo.application.isValid ) { 
      case 2: /* permissions? */
        appinfo.application.saverr = EACCES;
        break; 
      default: /* no such file? */
        appinfo.application.saverr = ENOENT; 
        break;
      }
      fputs( "FATAL: The main job specification is invalid or missing.\n",
             stderr );
      return 127;
    }
  } else {
    /* there is not even an application to run */
    helpMe( &appinfo );
  }

  /* make/change into new workdir NOW */
 REDIR:
  if ( workdir != NULL && chdir(workdir) != 0 ) {
    /* shall we try to make the directory */
    if ( createDir ) {
      createDir = 0; /* once only */

      if ( mkdir( workdir, 0777 ) == 0 ) {
        /* If this causes an infinite loop, your file-system is
         * seriously whacked out -- run fsck or equivalent. */
        goto REDIR;
      }
      /* else */
      appinfo.application.saverr = errno;
      debugmsg( "Unable to mkdir %s: %d: %s\n", 
               workdir, errno, strerror(errno) );
      appinfo.application.prefix = "Unable to mkdir: ";
      appinfo.application.status = -1;
      return 127;
    }

    /* unable to use alternate workdir */
    appinfo.application.saverr = errno;
    debugmsg( "Unable to chdir %s: %d: %s\n", 
             workdir, errno, strerror(errno) );
    appinfo.application.prefix = "Unable to chdir: ";
    appinfo.application.status = -1;
    return 127;
  }

  /* record the current working directory */
  appinfo.workdir = calloc(cwd_size,sizeof(char));
  if ( getcwd( appinfo.workdir, cwd_size ) == NULL && errno == ERANGE ) {
    /* error allocating sufficient space */
    free((void*) appinfo.workdir );
    appinfo.workdir = NULL;
  }

  /* update stdio and logfile *AFTER* we arrived in working directory */
  updateStatInfo( &appinfo.input );
  updateStatInfo( &appinfo.output );
  updateStatInfo( &appinfo.error );
  updateStatInfo( &appinfo.logfile );

  /* stat pre files */
  appinfo.initial = initStatFromList( &initial, &appinfo.icount );
  mylist_done( &initial );

  /* remember environment that all jobs will see */
  if ( ! appinfo.noHeader ) 
    envIntoAppInfo( &appinfo, environ );

  /* Our own initially: an independent setup job */
  if ( prepareSideJob( &appinfo.setup, getenv("GRIDSTART_SETUP") ) )
    mysystem( &appinfo, &appinfo.setup, environ );

  /* possible pre job */
  result = 0;
  if ( prepareSideJob( &appinfo.prejob, getenv("GRIDSTART_PREJOB") ) ) {
    /* there is a prejob to be executed */
    status = mysystem( &appinfo, &appinfo.prejob, environ );
    result = obtainStatusCode(status);
  }

  /* start main application */
  if ( result == 0 ) {
    status = mysystem( &appinfo, &appinfo.application, environ );
    result = obtainStatusCode(status);
  } else {
    /* actively invalidate main record */
    appinfo.application.isValid = 0;
  }

  /* possible post job */
  if ( result == 0 ) {
    if ( prepareSideJob( &appinfo.postjob, getenv("GRIDSTART_POSTJOB") ) ) {
      status = mysystem( &appinfo, &appinfo.postjob, environ );
      result = obtainStatusCode(status);
    }
  }

  /* Java's finally: an independent clean-up job */
  if ( prepareSideJob( &appinfo.cleanup, getenv("GRIDSTART_CLEANUP") ) )
    mysystem( &appinfo, &appinfo.cleanup, environ );

  /* stat post files */
  appinfo.final = initStatFromList( &final, &appinfo.fcount );
  mylist_done( &final );

  /* Record final result */
  appinfo.status = result;

  /* append results to log file */
  printAppInfo( &appinfo );

  /* clean up and close FDs */
  global_no_atexit = 1; /* disable atexit handler */
  deleteAppInfo( &appinfo );

  /* done */
  return result;
}
