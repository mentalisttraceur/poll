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
                         struct pollfd * polls)
{
    /*\
    If no prior FD arguments, increment nfds to use the default poll as this
    FD group:
    \*/
    if(!*nfds)
    {
        *nfds = 1;
    }
    /* Apply flags to FD group: */
    for(; *fdGroup_i < *nfds; *fdGroup_i += 1)
    {
        polls[*fdGroup_i].events = flags;
    }
}

int main(int argc, char * * argv)
{
    char * arg;
    char * arg0 = *argv;
    char * timeout_arg = 0;
    int timeout = -1;  /* default timeout is no timeout */
    int nfds;

    if(argc < 2)
    {
        if(!arg0)
        {
            arg0 = "";
        }
        return error_need_poll(arg0);
    }

    argv += 1;
    arg = *argv;
 
    if(*arg == '-')
    {
        arg += 1;
        if(!strcmp(arg, "-help") || !strcmp(arg, "h"))
        {
            return print_help(arg0);
        }
        if(!strcmp(arg, "-version") || !strcmp(arg, "V"))
        {
            return print_version(arg0);
        }

        if(!strcmp(arg, "-timeout") || !strcmp(arg, "t"))
        {
            argv += 1;
            arg = *argv;
            if(!arg)
            {
                return error_need_timeout(arg0);
            }
            timeout_arg = arg;
        }
        else
        if(!strncmp(arg, "-timeout=", 9))
        {
            timeout_arg = arg + 9;
        }
        else
        if(!strncmp(arg, "t", 1))
        {
            timeout_arg = arg + 1;
        }
        else
        /* If it is *not* the "end of options" ("--") "option": */
        if(strcmp(arg, "-"))
        {
            return error_bad_option(arg - 1, arg0);
        }

        argv += 1;
        arg = *argv;

        if(!arg)
        {
            return error_need_poll(arg0);
        }
    }

    if(timeout_arg && !parse_nonnegative_int(timeout_arg, &timeout))
    {
        return error_bad_timeout(timeout_arg, arg0);
    }

    /* We always poll for at least one FD if we poll at all. */
    nfds = argc;
 
    /*\
    This overallocates in most cases, but it is normal for calloc
    to overallocate much more internally (one memory page or more).
    \*/
    char * * fds = calloc(nfds, sizeof(char *));
    struct pollfd * polls = calloc(nfds, sizeof(struct pollfd));
    if(!fds || !polls)
    {
        return error_allocating_memory(arg0);
    }
 
    /* Now nfds will index into polls and fds */
    nfds = 0;

    polls[0].fd = 0;
    fds[0] = "0";
 
    short flags = 0;
    nfds_t fdGroup_i = 0;
 
    char * * argvCopy = argv;
    for(; *argv; argv += 1)
    {
        int fd;
        if(parse_nonnegative_int(*argv, &fd))
        {
            /* If there were flags since the last FD, we need to apply them: */
            if(flags)
            {
                applyFlagsToFDGroup(flags, &nfds, &fdGroup_i, polls);
                /* Reset flags for next group. */
                flags = 0;
            }
            polls[nfds].fd = fd;
            fds[nfds] = *argv;
            nfds += 1;
            continue;
        }
  
        short flag = strToEventFlag(*argv);
        if(flag)
        {
            flags |= flag;
            continue;
        }
  
        return error_bad_argument(*argv, arg0);
    }
    /* Need to apply flags to last FD group: */
    applyFlagsToFDGroup(flags, &nfds, &fdGroup_i, polls);
 
    /* Merge multiple entries for the same file descriptor. */
    for(fdGroup_i = 0; fdGroup_i < (nfds - 1); fdGroup_i += 1)
    {
        for(nfds_t i = fdGroup_i + 1; i < nfds; i += 1)
        {
            if(polls[i].fd == polls[fdGroup_i].fd)
            {
                polls[fdGroup_i].events |= polls[i].events;
                /* Fill up the now-unused hole in poll specification array: */
                nfds -= 1;
                polls[i] = polls[nfds];
                fds[i] = fds[nfds];
                i -= 1;
            }
        }
    }
 
    int result = poll(polls, nfds, timeout);
    if(result < 0)
    {
        return error_polling(arg0);
    }
    if(!result)
    {
        return EXIT_NO_EVENT;
    }
    int exitcode = EXIT_UNASKED_EVENT;
    for(fdGroup_i = 0; fdGroup_i < nfds && result; fdGroup_i += 1)
    {
        if(polls[fdGroup_i].revents)
        {
            printEventFlags(polls[fdGroup_i].revents, fds[fdGroup_i]);
            if(polls[fdGroup_i].revents & polls[fdGroup_i].events)
            {
                exitcode = EXIT_ASKED_EVENT_OR_INFO;
            }
            result -= 1;
        }
    }
    return exitcode;
}
