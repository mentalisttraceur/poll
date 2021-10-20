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

#include <errno.h> /* errno */
#include <stdio.h> /* fputc, fputs, perror, stderr, stdout */
#include <stdlib.h> /* malloc */
#include <string.h> /* strlen, strcmp, strncmp, strerror */
#include <poll.h> /* all poll-related definitions */

#define STR_m(text) #text
#define STR_MACRO_m(macro) STR_m(macro)

#define EXIT_POLLED_EVENT_OR_INFO 0
#define EXIT_UNPOLLED_EVENT 1
#define EXIT_NO_EVENT 2
#define EXIT_USAGE_ERROR 3
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
    "  " STR_MACRO_m(EXIT_USAGE_ERROR)
          "  Syntax error in how the poll command was called.\n"
    "  " STR_MACRO_m(EXIT_EXECUTION_ERROR)
          "  Error when trying to carry out the poll command.\n"
;

char const eventList[] =
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
#define OPTION_PARSE_continue 2
int parseOption(char * * * strsPtr, int * timeoutPtr)
{
    char * * strs = *strsPtr;
    char * str = *strs;
 
    str += 1;
    if(!strcmp(str, "-help") || !strcmp(str, "h"))
    {
        fputs(helpText, stdout);
        return OPTION_PARSE_exit_success;
    }
    if(!strcmp(str, "-help-events"))
    {
        fputs(eventList, stdout);
        return OPTION_PARSE_exit_success;
    }
    if(!strcmp(str, "-help-exits"))
    {
        fputs(exitCodes, stdout);
        return OPTION_PARSE_exit_success;
    }
 
    if(!strcmp(str, "-timeout") || !strcmp(str, "t"))
    {
        strs += 1;
        str = *strs;
        if(!str)
        {
            fputs(timeoutMissing, stderr);
            return OPTION_PARSE_exit_failure;
        }
        *strsPtr = strs;
    }
    else
    if(!strncmp(str, "-timeout=", 9))
    {
        str += 9;
    }
    else
    if(str[0] == 't')
    {
        str += 1;
    }
    else
    {
        fputs(unrecognizedOption, stderr);
        fputs(str, stderr);
        fputc('\n', stderr);
        return OPTION_PARSE_continue;
    }
 
    size_t len;
    int timeout = strToInt(str, &len);
    if(timeout >= 0)
    {
        *timeoutPtr = timeout;
        return OPTION_PARSE_continue;
    }
    else
    if(timeout == STR_TO_INT_overflow)
    {
        fputs(timeoutOverflowedInt, stderr);
        fputs(str, stderr);
        fputc('\n', stderr);
        return OPTION_PARSE_exit_failure;
    }
    else
    if(timeout == STR_TO_INT_invalid)
    {
        fputs(timeoutInvalid, stderr);
        fputs(str, stderr);
        fputc('\n', stderr);
        return OPTION_PARSE_exit_failure;
    }
    /* should not be reached */
    return OPTION_PARSE_exit_failure;
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
    fputs(outputBuffer, stdout);
}

static void applyFlagsToFDGroup(short flags, nfds_t * nfds, nfds_t * fdGroup_i,
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
        fputs(unableToMalloc, stderr);
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
            fputs(fdOverflowedInt, stderr);
            fputs(*argv, stderr);
            fputc('\n', stderr);
            return EXIT_USAGE_ERROR;
        }
  
        if(*argv[0] != '-')
        {
            int optionParseResult = parseOption(&argv, &timeout);
            if(optionParseResult == OPTION_PARSE_exit_success)
                return EXIT_POLLED_EVENT_OR_INFO;
            if(optionParseResult == OPTION_PARSE_exit_failure)
                return EXIT_USAGE_ERROR;
            continue;
        }
  
        short flag = strToEventFlag(*argv);
        if(flag)
        {
            flags |= flag;
            continue;
        }
  
        fputs(unrecognizedEvent, stderr);
        fputs(*argv, stderr);
        fputc('\n', stderr);
        return EXIT_USAGE_ERROR;
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
        perror(pollError);
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
