/* SPDX-License-Identifier: 0BSD */
/* Copyright 2015 Alexander Kozhevnikov <mentalisttraceur@gmail.com> */

/*\
NOTE: some poll events might not be defined when compiling with default
settings, depending on your system, compiler, etc. Typically, this can
be fixed by defining the right macro to enable/expose those events,
such as `_XOPEN_SOURCE` or `_GNU_SOURCE`.
\*/

#include <stddef.h> /* size_t */
#include <stdbool.h> /* bool */
#include <limits.h> /* INT_MAX */

#include <unistd.h> /* write */
#include <stdio.h> /* fprintf, perror */
#include <string.h> /* strlen, strcmp, strncmp */
#include <poll.h> /* all poll-related definitions */

#define STR_m(text) #text
#define STR_MACRO_m(macro) STR_m(macro)

#define EXIT_POLLED_EVENT_OR_INFO 0
#define EXIT_UNPOLLED_EVENT 1
#define EXIT_NO_EVENT 2
#define EXIT_SYNTAX_ERROR 3
#define EXIT_EXECUTION_ERROR 4

char const unrecognizedOption[] = "poll: Unrecognized option: ";
char const unrecognizedEvent[] = "poll: Unrecognized event: ";
char const fdOverflowedInt[]
= "poll: FD value greater than maximum possible: ";
char const timeoutOverflowedInt[]
= "poll: timeout value greater than maximum possible: ";
char const timeoutMissing[] = "poll: timeout option requires an argument\n";
char const timeoutInvalid[] = "poll: invalid timeout value: ";

char const helpText[] =
 "\n"
 "Usage: poll [OPTIONS] [FD] [EVENT]...\n"
 "\n"
 "Poll FD (file descriptor, default is 0)* for events of interest.\n"
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
 /*short static const requestableEventFlagCount = eventFlagCount - 3;*/
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
   return STR_TO_INT_invalid;
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
  return STR_TO_INT_overflow;
 }
 if(lenPtr)
 {
  *lenPtr = len;
 }
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
  write(1, eventList + 1, sizeof(eventList) - 1);
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
  fprintf(stderr, "%s%s%s", unrecognizedOption, str - 1, helpText);
  return OPTION_PARSE_exit_failure;
 }
 
 int timeout = strToInt(str, 0);
 if(timeout >= 0)
 {
  *timeoutPtr = timeout;
  return OPTION_PARSE_parse_good;
 }
 else
 if(timeout == STR_TO_INT_overflow)
 {
  size_t len = strlen(str);
  str[len] = '\n';
  write(2, timeoutOverflowedInt, sizeof(timeoutOverflowedInt) - 1);
  write(2, str, len + 1);
  return OPTION_PARSE_exit_failure;
 }
 else
 if(timeout == STR_TO_INT_invalid)
 {
  size_t len = strlen(str);
  str[len] = '\n';
  write(2, timeoutInvalid, sizeof(timeoutInvalid) - 1);
  write(2, str, len + 1);
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

void printEventFlags(short flags, char const * const fdStr, size_t len)
{
 char outputBuffer[MAX_OUTPUT_LEN];
 char * outputBufferPtr = outputBuffer + len;
 memcpy(outputBuffer, fdStr, len);
 for(size_t i = 0; i < EVENT_FLAG_COUNT; i += 1)
 {
  if(eventFlagMaps[i].flag & flags)
  {
   *outputBufferPtr = ' ';
   outputBufferPtr += 1;
   len = strlen(eventFlagMaps[i].name);
   memcpy(outputBufferPtr, eventFlagMaps[i].name, len);
   outputBufferPtr += len;
  }
 }
 *outputBufferPtr = '\n';
 write(1, outputBuffer, outputBufferPtr + 1 - outputBuffer);
}

int main(int argc, char * * argv)
{
 struct pollfd pollData;
 pollData.fd = 0;
 pollData.events = 0;
 int timeout = -1, exitcode = 0;
 char const * fdStr = "0";
 size_t fdStrLen = 1;
 for(argv += 1; *argv; argv += 1)
 {
  int fd = strToInt(*argv, &fdStrLen);
  if(fd >= 0)
  {
   pollData.fd = fd;
   fdStr = *argv;
   continue;
  }
  else
  if(fd == STR_TO_INT_overflow)
  {
   size_t len = strlen(*argv);
   (*argv)[len] = '\n';
   write(2, fdOverflowedInt, sizeof(fdOverflowedInt) - 1);
   write(2, *argv, len + 1);
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
   pollData.events |= flag;
   continue;
  }
  
  fprintf(stderr, "%s%s%s", unrecognizedEvent, *argv, eventList);
  return EXIT_SYNTAX_ERROR;
 }
 
 int result = poll(&pollData, 1, timeout);
 if(result < 0)
 {
  perror("poll: ");
  return EXIT_POLL_ERROR;
 }
 if(!result)
 {
  return EXIT_NO_EVENT;
 }
 printEventFlags(pollData.revents, fdStr, fdStrLen);
 if(pollData.revents & pollData.events)
 {
  return EXIT_POLLED_EVENT_OR_INFO;
 }
 return EXIT_UNPOLLED_EVENT;
}
