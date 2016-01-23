/*****************************************************************************\
 * poll 1.0.3
 * Copyright (C) 2016-01-23 Alexander Kozhevnikov <mentalisttraceur@gmail.com>
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public Licence as published by
 * the Free Software Foundation, either version 3 of the licence or,
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for details.
 * 
 * You should've received a copy of the GNU General Public License
 * with this program. If not, see <http://www.gnu.org/licences/>,
 * or write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330 Boston MA 02111-1307 USA.
\*****************************************************************************/

/*\
NOTE: some poll events might not be defined when compiling with default
settings, depending on your system, compiler, etc. Typically, this can be
fixed by defining the right macro to enable/expose those events, such as
_XOPEN_SOURCE or _GNU_SOURCE
\*/

#include <stddef.h> /* size_t */
#include <stdbool.h> /* bool */
#include <limits.h> /* INT_MAX */

#include <unistd.h> /* write */
#include <sys/uio.h> /* writev, struct iovec */
#include <errno.h> /* errno */
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen, strcmp, strncmp, strerror */
#include <poll.h> /* all poll-related definitions */

#define STR_m(text) #text
#define STR_MACRO_m(macro) STR_m(macro)

#define EXIT_POLLED_EVENT_OR_INFO 0
#define EXIT_UNPOLLED_EVENT 1
#define EXIT_NO_EVENT 2
#define EXIT_SYNTAX_ERROR 3
#define EXIT_EXECUTION_ERROR 4

typedef struct
{
 size_t n;
 char * m;
}
nstr_st;

char const unrecognizedOption[] = "poll: Unrecognized option: ";
char const unrecognizedEvent[] = "poll: Unrecognized event: ";
char const fdOverflowedInt[]
= "poll: FD value greater than maximum possible: ";
char const timeoutOverflowedInt[]
= "poll: timeout value greater than maximum possible: ";
char const timeoutMissing[] = "poll: timeout option requires an argument\n";
char const timeoutInvalid[] = "poll: invalid timeout value: ";
char const unableToMalloc[] = "poll: unable to allocate memory\n";
char const pollError[] = "poll: error polling: ";

char const helpText[] =
 "\n"
 "Usage: poll [OPTION] [[FD]... [EVENT]...]...\n"
 "\n"
 "Poll FDs (file descriptors, default is 0)* for events of interest.\n"
 "\n"
 "  -h, --help            Print this help text and exit.\n"
 "      --help-events     List possible FD events and exit.\n"
 "      --help-exits      List exit code meanings and exit.\n"
 "  -t, --timeout=TIMEOUT How long to wait for events (in milliseconds).\n"
 "\n"
 " * File descriptors are expected to be non-negative integers.\n"
;
 
char const exitCodes[] =
 "Exit codes:\n"
 "\n"
 "  " STR_MACRO_m(EXIT_POLLED_EVENT_OR_INFO)
       "  A polled event occurred, or help info printed.\n"
 "  " STR_MACRO_m(EXIT_UNPOLLED_EVENT)
       "  An always-polled event that was not explicitly polled occurred.\n"
 "  " STR_MACRO_m(EXIT_NO_EVENT)
       "  No events occurred before timeout ended.\n"
 "  " STR_MACRO_m(EXIT_SYNTAX_ERROR)
       "  Syntax error in how the poll command was called.\n"
 "  " STR_MACRO_m(EXIT_EXECUTION_ERROR)
       "  Error when trying to carry out the poll command.\n"
;

char const eventList[] =
 "\n"
 "Pollable events:\n"
 "  IN PRI OUT"
#ifdef POLLRDNORM
 " RDNORM"
#endif
#ifdef POLLRDBAND
 " RDBAND"
#endif
#ifdef POLLWRNORM
 " WRNORM"
#endif
#ifdef POLLWRBAND
 " WRBAND"
#endif
#ifdef POLLMSG
 " MSG"
#endif
#ifdef POLLREMOVE
 " REMOVE"
#endif
#ifdef POLLRDHUP
 " RDHUP"
#endif
 "\n"
 "\n"
 "Always-polled events (polling these only effects exit code if they occur):\n"
 "  ERR HUP NVAL\n"
 "\n"
 "See your system's poll documentation for each event's exact meaning.\n"
;

typedef struct
{
 short const flag;
 char const * const restrict name;
}
eventFlagMap_st;

#define eventFlagMap_m(name) { POLL ## name, # name }

eventFlagMap_st const eventFlagMaps[] =
{
 eventFlagMap_m(IN),
 eventFlagMap_m(PRI),
 eventFlagMap_m(OUT),
/* These flags used to be in a POSIX extention: sometimes undefined. */
#ifdef POLLRDNORM
 eventFlagMap_m(RDNORM),
#endif
#ifdef POLLRDBAND
 eventFlagMap_m(RDBAND),
#endif
#ifdef POLLWRNORM
 eventFlagMap_m(WRNORM),
#endif
#ifdef POLLWRBAND
 eventFlagMap_m(WRBAND),
#endif
/* These flags seem to be Linux/GNU -specific: typically undefined. */
#ifdef POLLMSG
 eventFlagMap_m(MSG),
#endif
#ifdef POLLREMOVE
 eventFlagMap_m(REMOVE),
#endif
#ifdef POLLRDHUP
 eventFlagMap_m(RDHUP),
#endif
/* Please let me know of additional poll flags on other systems */
/* result-only flags go at the bottom, so that command-line arguments are
checked against them last - they are ignored in the "events" field on all
systems as far as I know, so this code allows them to be set when polling by
inclusion in command-line */
 eventFlagMap_m(ERR),
 eventFlagMap_m(HUP),
 eventFlagMap_m(NVAL)
/* Please feel free to inform me or submit patches for other additional poll
flags on other systems. */
};

bool strIsEventFlagName
(char const * restrict str, char const * restrict eventFlagName)
{
 for
 (
  char str_c, eventFlagName_c;
  (str_c = *str) && (eventFlagName_c = *eventFlagName);
  str += 1, eventFlagName += 1
 )
 {
  if(str_c >= 'a' && str_c <= 'z')
  {
   str_c -= 32;
  }
  if(str_c != eventFlagName_c)
  {
   return 0;
  }
 }
 return 1;
}

#define EVENT_FLAG_COUNT (sizeof(eventFlagMaps) / sizeof(eventFlagMap_st))

short strToEventFlag(char const * str)
{
 for(size_t i = 0; i < EVENT_FLAG_COUNT; i += 1)
 {
  if(strIsEventFlagName(str, eventFlagMaps[i].name))
  {
   return eventFlagMaps[i].flag;
  }
 }
 return 0;
}

#define STR_TO_INT_overflow -1
#define STR_TO_INT_invalid -2
int strToInt(char const * str, size_t * lenPtr)
{
 int val = 0;
 size_t len = 0;
 for(int c; c = *str; str += 1)
 {
  if(c < '0' || c > '9')
  {
   val = STR_TO_INT_invalid;
   break;
  }
  if(val <= INT_MAX / 10)
  {
   val *= 10;
   c -= '0';
   if(val <= INT_MAX - c)
   {
    val += c;
    len += 1;
    continue;
   }
  }
  val = STR_TO_INT_overflow;
  break;
 }
 *lenPtr = len + strlen(str);
 return val;
}

#define OPTION_PARSE_exit_success 0
#define OPTION_PARSE_exit_failure 1
#define OPTION_PARSE_parse_good 2
#define OPTION_PARSE_parse_bad 3
int parseOption(char * * * strsPtr, int * timeoutPtr)
{
 char * * strs = *strsPtr;
 char * str = *strs;
 
 if(str[0] != '-')
 {
  return OPTION_PARSE_parse_bad;
 }
 str += 1;
 if(!strcmp(str, "h") || !strcmp(str, "-help"))
 {
  write(1, helpText + 1, sizeof(helpText) - 2);
  return OPTION_PARSE_exit_success;
 }
 if(!strcmp(str, "-help-events"))
 {
  write(1, eventList + 1, sizeof(eventList) - 2);
  return OPTION_PARSE_exit_success;
 }
 if(!strcmp(str, "-help-exits"))
 {
  write(1, exitCodes, sizeof(exitCodes) - 1);
  return OPTION_PARSE_exit_success;
 }
 
 if(!strcmp(str, "t") || !strcmp(str, "-timeout"))
 {
  strs += 1;
  str = *strs;
  if(!str)
  {
   write(2, timeoutMissing, sizeof(timeoutMissing) - 1);
   return OPTION_PARSE_exit_failure;
  }
  *strsPtr = strs;
 }
 else
 if(str[0] == 't')
 {
  str += 1;
 }
 else
 if(!strncmp(str, "-timeout=", 9))
 {
  str += 9;
 }
 else
 {
  struct iovec errMsg[3];
  errMsg[0].iov_base = (void * )unrecognizedOption;
  errMsg[0].iov_len = sizeof(unrecognizedOption) - 1;
  str -= 1;
  errMsg[1].iov_base = str;
  errMsg[1].iov_len = strlen(str);
  errMsg[2].iov_base = (void * )helpText;
  errMsg[2].iov_len = sizeof(helpText) - 1;
  writev(2, errMsg, 3);
  return OPTION_PARSE_exit_failure;
 }
 
 size_t len;
 int timeout = strToInt(str, &len);
 if(timeout >= 0)
 {
  *timeoutPtr = timeout;
  return OPTION_PARSE_parse_good;
 }
 else
 if(timeout == STR_TO_INT_overflow)
 {
  str[len] = '\n';
  struct iovec errMsg[2];
  errMsg[0].iov_base = (void * )timeoutOverflowedInt;
  errMsg[0].iov_len = sizeof(timeoutOverflowedInt) - 1;
  errMsg[1].iov_base = str;
  errMsg[1].iov_len = len + 1;
  writev(2, errMsg, 2);
  return OPTION_PARSE_exit_failure;
 }
 else
 if(timeout == STR_TO_INT_invalid)
 {
  str[len] = '\n';
  struct iovec errMsg[2];
  errMsg[0].iov_base = (void * )timeoutInvalid;
  errMsg[0].iov_len = sizeof(timeoutInvalid) - 1;
  errMsg[1].iov_base = str;
  errMsg[1].iov_len = len + 1;
  writev(2, errMsg, 2);
  return OPTION_PARSE_exit_failure;
 }
 return OPTION_PARSE_parse_bad;
}

const size_t MAX_OUTPUT_LEN = sizeof
(
 STR_MACRO_m(INT_MAX) " IN PRI OUT ERR HUP NVAL\n"
#ifdef POLLRDNORM
 " RDNORM"
#endif
#ifdef POLLRDBAND
 " RDBAND"
#endif
#ifdef POLLWRNORM
 " WRNORM"
#endif
#ifdef POLLWRBAND
 " WRBAND"
#endif
#ifdef POLLMSG
 " MSG"
#endif
#ifdef POLLREMOVE
 " REMOVE"
#endif
#ifdef POLLRDHUP
 " RDHUP"
#endif
);

void printEventFlags(short flags, nstr_st fdNStr)
{
 char outputBuffer[MAX_OUTPUT_LEN];
 char * outputBufferPtr = outputBuffer + fdNStr.n;
 memcpy(outputBuffer, fdNStr.m, fdNStr.n);
 for(size_t i = 0; i < EVENT_FLAG_COUNT; i += 1)
 {
  if(eventFlagMaps[i].flag & flags)
  {
   *outputBufferPtr = ' ';
   outputBufferPtr += 1;
   size_t len = strlen(eventFlagMaps[i].name);
   memcpy(outputBufferPtr, eventFlagMaps[i].name, len);
   outputBufferPtr += len;
  }
 }
 *outputBufferPtr = '\n';
 write(1, outputBuffer, outputBufferPtr + 1 - outputBuffer);
}

/*\
Repeated code hoisted up into inline function (I personally think a macro is
better, but I presume this is nicer for people's syntax highlighting, etc.)
\*/
inline void applyFlagsToFDGroup(short flags, nfds_t * nfds, nfds_t * fdGroup_i,
 struct pollfd * pollSpecs, nstr_st * fdNStrs)
{
 /*\
 If no prior FD arguments, increment nfds to use the default pollSpec as this
 FD group:
 \*/
 if(!*nfds)
 {
  *nfds = 1;
 }
 /* Apply flags to FD group: */
 for(; *fdGroup_i < *nfds; *fdGroup_i += 1)
 {
  pollSpecs[*fdGroup_i].events = flags;
 }
}

int main(int argc, char * * argv)
{
 /* Zeroth argument is the program name itself, skip it. */
 argv += 1;
 argc -= 1;
 
 /* We always poll for at least one FD if we poll at all. */
 nfds_t nfds;
 if(argc < 1)
 {
  nfds = 1;
 }
 else
 {
  nfds = argc;
 }
 
 /*\
 This "overallocates" a few slots in most cases, but on most platforms, malloc
 will overallocate much more internally (one memory page or more) either way.
 \*/
 nstr_st * fdNStrs = malloc(nfds * (sizeof(nstr_st) + sizeof(struct pollfd)));
 if(!fdNStrs)
 {
  write(2, unableToMalloc, sizeof(unableToMalloc) - 1);
  return EXIT_EXECUTION_ERROR;
 }
 struct pollfd * pollSpecs = (void * )(fdNStrs + nfds);
 
 /* Now nfds will index into pollSpecs and fdNStrs */
 nfds = 0;

 pollSpecs[0].fd = 0;
 fdNStrs[0] = (nstr_st ){ .n = 1, .m = "0" };
 
 /* Default timeout is no timeout */
 int timeout = -1;
 
 short flags = 0;
 nfds_t fdGroup_i = 0;
 
 char * * argvCopy = argv;
 for(; *argv; argv += 1)
 {
  size_t len;
  int fd = strToInt(*argv, &len);
  if(fd >= 0)
  {
   /* If there were flags since the last FD, we need to apply them: */
   if(flags)
   {
    applyFlagsToFDGroup(flags, &nfds, &fdGroup_i, pollSpecs, fdNStrs);
    /* Reset flags for next group. */
    flags = 0;
   }
   pollSpecs[nfds].fd = fd;
   fdNStrs[nfds] = (nstr_st ){ .n = len, .m = *argv };
   nfds += 1;
   continue;
  }
  else
  if(fd == STR_TO_INT_overflow)
  {
   (*argv)[len] = '\n';
   struct iovec errMsg[2];
   errMsg[0].iov_base = (void * )fdOverflowedInt;
   errMsg[0].iov_len = sizeof(fdOverflowedInt) - 1;
   errMsg[1].iov_base = *argv;
   errMsg[1].iov_len = len + 1;
   writev(2, errMsg, 2);
   return EXIT_SYNTAX_ERROR;
  }
  
  int optionParseResult = parseOption(&argv, &timeout);
  if(optionParseResult == OPTION_PARSE_exit_success)
  {
   return EXIT_POLLED_EVENT_OR_INFO;
  }
  else
  if(optionParseResult == OPTION_PARSE_exit_failure)
  {
   return EXIT_SYNTAX_ERROR;
  }
  else
  if(optionParseResult == OPTION_PARSE_parse_good)
  {
   continue;
  }
  
  short flag = strToEventFlag(*argv);
  if(flag)
  {
   flags |= flag;
   continue;
  }
  
  struct iovec errMsg[3];
  errMsg[0].iov_base = (void * )unrecognizedEvent;
  errMsg[0].iov_len = sizeof(unrecognizedEvent) - 1;
  errMsg[1].iov_base = *argv;
  errMsg[1].iov_len = len;
  errMsg[2].iov_base = (void * )eventList;
  errMsg[2].iov_len = sizeof(eventList) - 1;
  writev(2, errMsg, 3);
  return EXIT_SYNTAX_ERROR;
 }
 /* Need to apply flags to last FD group: */
 applyFlagsToFDGroup(flags, &nfds, &fdGroup_i, pollSpecs, fdNStrs);
 
 /* Merge multiple entries for the same file descriptor. */
 for(fdGroup_i = 0; fdGroup_i < (nfds - 1); fdGroup_i += 1)
 {
  for(nfds_t i = fdGroup_i + 1; i < nfds; i += 1)
  {
   if(pollSpecs[i].fd == pollSpecs[fdGroup_i].fd)
   {
    pollSpecs[fdGroup_i].events |= pollSpecs[i].events;
    /* Fill up the now-unused hole in poll specification array: */
    nfds -= 1;
    pollSpecs[i] = pollSpecs[nfds];
    fdNStrs[i] = fdNStrs[nfds];
    i -= 1;
   }
  }
 }
 
 int result = poll(pollSpecs, nfds, timeout);
 if(result < 0)
 {
  struct iovec errMsg[2];
  errMsg[0].iov_base = (void * )pollError;
  errMsg[0].iov_len = sizeof(pollError) - 1;
  char * errStr = strerror(errno);
  errMsg[1].iov_base = errStr;
  errMsg[1].iov_len = strlen(errStr);
  writev(2, errMsg, 2);
  return EXIT_EXECUTION_ERROR;
 }
 if(!result)
 {
  return EXIT_NO_EVENT;
 }
 int exitcode = EXIT_UNPOLLED_EVENT;
 for(fdGroup_i = 0; fdGroup_i < nfds && result; fdGroup_i += 1)
 {
  if(pollSpecs[fdGroup_i].revents)
  {
   printEventFlags(pollSpecs[fdGroup_i].revents, fdNStrs[fdGroup_i]);
   if(pollSpecs[fdGroup_i].revents & pollSpecs[fdGroup_i].events)
   {
    exitcode = EXIT_POLLED_EVENT_OR_INFO;
   }
   result -= 1;
  }
 }
 return exitcode;
}
