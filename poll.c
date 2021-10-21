/* SPDX-License-Identifier: 0BSD */
/* Copyright 2015 Alexander Kozhevnikov <mentalisttraceur@gmail.com> */

/*\
NOTE: some poll events might not be defined by default unless you
define a processor macro such as `_XOPEN_SOURCE` or `_GNU_SOURCE`
before any `#include` directive.
\*/

/* Standard C library headers */
#include <errno.h> /* errno */
#include <limits.h> /* INT_MAX */
#include <stddef.h> /* size_t */
#include <stdio.h> /* EOF, fputc, fputs, perror, stderr, stdout */
#include <stdlib.h> /* calloc */
#include <string.h> /* strlen, strcmp, strncmp */

/* Standard UNIX/Linux (POSIX/SUS base) headers */
#include <poll.h> /* POLL*, nfds_t, poll, struct pollfd */


#define STR_m(text) #text
#define STR_MACRO_m(macro) STR_m(macro)

#define EXIT_ASKED_EVENT_OR_INFO 0
#define EXIT_UNASKED_EVENT 1
#define EXIT_NO_EVENT 2
#define EXIT_USAGE_ERROR 3
#define EXIT_EXECUTION_ERROR 4


char const version_text[] = "poll 1.0.0";

char const help_text[] =
    "Wait until at least one event happens on at least one file descriptor.\n"
    "\n"
    "Usage:\n"
    "    poll [--timeout=<timeout>] [[<descriptor>]... [<event>]...]...\n"
    "    poll (--help | --version)\n"
    "\n"
    "Options:\n"
    "    -h --help               show this help text\n"
    "    -V --version            show version text\n"
    "    -t --timeout=<timeout>  upper limit on waiting (in milliseconds)\n"
    "\n"
    "Exits:\n"
    "    " STR_MACRO_m(EXIT_ASKED_EVENT_OR_INFO)
    "  got at least one event that was asked for\n"
    "    " STR_MACRO_m(EXIT_UNASKED_EVENT)
    "  got only always-polled events that were not asked for\n"
    "    " STR_MACRO_m(EXIT_NO_EVENT)
    "  got no events within <timeout> milliseconds\n"
    "    " STR_MACRO_m(EXIT_USAGE_ERROR)
    "  error in how the poll command was called\n"
    "    " STR_MACRO_m(EXIT_EXECUTION_ERROR)
    "  error when trying to carry out the poll command\n"
    "\n"
    "Normal events:\n"
    "    IN OUT PRI"
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
    "Always-polled events:\n"
    "    ERR HUP NVAL\n"
;

typedef struct
{
    short const flag;
    char const * const name;
}
eventFlagMap_st;

#define eventFlagMap_m(name) { POLL ## name, # name }

eventFlagMap_st const eventFlagMaps[] =
{
    eventFlagMap_m(IN),
    eventFlagMap_m(OUT),
    eventFlagMap_m(PRI),
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


static
int error_need_poll(char * arg0)
{
    if(fputs(arg0, stderr) != EOF)
    {
        fputs(": need file descriptor or event argument", stderr);
    }
    return EXIT_USAGE_ERROR;
}


static
int error_need_timeout(char * arg0)
{
    if(fputs(arg0, stderr) != EOF)
    {
        fputs(": need timeout argument", stderr);
    }
    return EXIT_USAGE_ERROR;
}


static
int error_bad_option(char * option, char * arg0)
{
    if(fputs(arg0, stderr) != EOF
    && fputs(": bad option: ", stderr) != EOF
    && fputs(option, stderr) != EOF)
    {
        fputc('\n', stderr);
    }
    return EXIT_USAGE_ERROR;
}


static
int error_bad_timeout(char * timeout, char * arg0)
{
    if(fputs(arg0, stderr) != EOF
    && fputs(": bad timeout: ", stderr) != EOF
    && fputs(timeout, stderr) != EOF)
    {
        fputc('\n', stderr);
    }
    return EXIT_USAGE_ERROR;
}


static
int error_bad_argument(char * argument, char * arg0)
{
    if(fputs(arg0, stderr) != EOF
    && fputs(": bad argument: ", stderr) != EOF
    && fputs(argument, stderr) != EOF)
    {
        fputc('\n', stderr);
    }
    return EXIT_USAGE_ERROR;
}


static
int error_writing_output(char * arg0)
{
    int errno_ = errno;
    if(fputs(arg0, stderr) != EOF)
    {
        errno = errno_;
        perror(": error writing output");
    }
    return EXIT_EXECUTION_ERROR;
}


static
int error_allocating_memory(char * arg0)
{
    int errno_ = errno;
    if(fputs(arg0, stderr) != EOF)
    {
        errno = errno_;
        perror(": error allocating memory");
    }
    return EXIT_EXECUTION_ERROR;
}


static
int error_polling(char * arg0)
{
    int errno_ = errno;
    if(fputs(arg0, stderr) != EOF)
    {
        errno = errno_;
        perror(": error polling");
    }
    return EXIT_EXECUTION_ERROR;
}


static
int print_help(char * arg0)
{
    if(fputs(help_text, stdout) != EOF
    && fflush(stdout) ! EOF)
    {
        return EXIT_ASKED_EVENT_OR_INFO;
    }
    return error_writing_output(arg0);
}


static
int print_version(char * arg0)
{
    if(fputs(version_text, stdout) != EOF
    && fflush(stdout) ! EOF)
    {
        return EXIT_ASKED_EVENT_OR_INFO;
    }
    return error_writing_output(arg0);
}


int strIsEventFlagName(char const * str, char const * eventFlagName)
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

static
int parse_nonnegative_int(char const * string, int * destination)
{
    int character, digit, value = 0;
    while((character = *string++))
    {
        if(character < '0' || character > '9')
        {
            return 0;
        }
        digit = character - '0';
        if(value > (INT_MAX - digit) / 10)
        {
            return 0;
        }
        value = (value * 10) + digit;
    }
    *destination = value;
    return 1;
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
 
    int timeout = strToInt(str);
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
    STR_MACRO_m(INT_MAX) " IN OUT PRI ERR HUP NVAL\n"
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

void printEventFlags(short flags, char * fdStr)
{
    char outputBuffer[MAX_OUTPUT_LEN];
    char * outputBufferPtr = outputBuffer + strlen(fdStr);
    strcpy(outputBuffer, fdStr);
    for(size_t i = 0; i < EVENT_FLAG_COUNT; i += 1)
    {
        if(eventFlagMaps[i].flag & flags)
        {
            *outputBufferPtr = ' ';
            outputBufferPtr += 1;
            strcpy(outputBufferPtr, eventFlagMaps[i].name);
            outputBufferPtr += strlen(eventFlagMaps[i].name);
        }
    }
    *outputBufferPtr = '\n';
    fputs(outputBuffer, stdout);
}

static
void applyFlagsToFDGroup(short flags, nfds_t * nfds, nfds_t * fdGroup_i,
                         struct pollfd * pollSpecs)
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
    This overallocates in most cases, but it is normal for calloc
    to overallocate much more internally (one memory page or more).
    \*/
    char * * fdStrs = calloc(nfds, sizeof(char *));
    struct pollfd * pollSpecs = calloc(nfds, sizeof(struct pollfd));
    if(!fdStrs || !pollSpecs)
    {
        fputs(unableToMalloc, stderr);
        return EXIT_EXECUTION_ERROR;
    }
 
    /* Now nfds will index into pollSpecs and fdStrs */
    nfds = 0;

    pollSpecs[0].fd = 0;
    fdStrs[0] = "0";
 
    /* Default timeout is no timeout */
    int timeout = -1;
 
    short flags = 0;
    nfds_t fdGroup_i = 0;
 
    char * * argvCopy = argv;
    for(; *argv; argv += 1)
    {
        int fd = strToInt(*argv);
        if(fd >= 0)
        {
            /* If there were flags since the last FD, we need to apply them: */
            if(flags)
            {
                applyFlagsToFDGroup(flags, &nfds, &fdGroup_i, pollSpecs);
                /* Reset flags for next group. */
                flags = 0;
            }
            pollSpecs[nfds].fd = fd;
            fdStrs[nfds] = *argv;
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
  
        if(*argv[0] == '-')
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
    applyFlagsToFDGroup(flags, &nfds, &fdGroup_i, pollSpecs);
 
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
                fdStrs[i] = fdStrs[nfds];
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
    int exitcode = EXIT_UNASKED_EVENT;
    for(fdGroup_i = 0; fdGroup_i < nfds && result; fdGroup_i += 1)
    {
        if(pollSpecs[fdGroup_i].revents)
        {
            printEventFlags(pollSpecs[fdGroup_i].revents, fdStrs[fdGroup_i]);
            if(pollSpecs[fdGroup_i].revents & pollSpecs[fdGroup_i].events)
            {
                exitcode = EXIT_ASKED_EVENT_OR_INFO;
            }
            result -= 1;
        }
    }
    return exitcode;
}
