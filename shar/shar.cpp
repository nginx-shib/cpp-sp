/*
 *  Copyright 2001-2005 Internet2
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * shar.cpp -- the shibd "main" code.  All the functionality is elsewhere
 *
 * Created By:	Derek Atkins <derek@ihtfp.com>
 *
 * $Id$
 */


// eventually we might be able to support autoconf via cygwin...
#if defined (_MSC_VER) || defined(__BORLANDC__)
# include "config_win32.h"
#else
# include "config.h"
#endif

#ifdef WIN32
# define _CRT_NONSTDC_NO_DEPRECATE 1
# define _CRT_SECURE_NO_DEPRECATE 1
#endif

#include <shib-target/shib-target.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#include <sys/select.h>
#endif

#include <stdio.h>
#include <signal.h>
#include <log4cpp/Category.hh>

using namespace std;
using namespace saml;
using namespace shibboleth;
using namespace shibtarget;
using namespace log4cpp;

bool shibd_shutdown = false;
const char* shar_config = NULL;
const char* shar_schemadir = NULL;
bool shar_checkonly = false;
static int unlink_socket = 0;
const char* pidfile = NULL;

#ifdef WIN32

//#include <CRTDBG.H>

#define nNoMansLandSize 4
typedef struct _CrtMemBlockHeader
{
        struct _CrtMemBlockHeader * pBlockHeaderNext;
        struct _CrtMemBlockHeader * pBlockHeaderPrev;
        char *                      szFileName;
        int                         nLine;
        size_t                      nDataSize;
        int                         nBlockUse;
        long                        lRequest;
        unsigned char               gap[nNoMansLandSize];
        /* followed by:
         *  unsigned char           data[nDataSize];
         *  unsigned char           anotherGap[nNoMansLandSize];
         */
} _CrtMemBlockHeader;

/*
int MyAllocHook(int nAllocType, void *pvData,
      size_t nSize, int nBlockUse, long lRequest,
      const unsigned char * szFileName, int nLine)
{
    if ( nBlockUse == _CRT_BLOCK )
      return( TRUE );
    if (nAllocType == _HOOK_FREE) {
        _CrtMemBlockHeader* ptr = (_CrtMemBlockHeader*)(((_CrtMemBlockHeader *)pvData)-1);
        if (ptr->nDataSize == 8192)
            fprintf(stderr,"free  request %u size %u\n", ptr->lRequest, ptr->nDataSize);
    }
    else if (nAllocType == _HOOK_ALLOC && nSize == 8192)
        fprintf(stderr,"%s request %u size %u\n", ((nAllocType == _HOOK_ALLOC) ? "alloc" : "realloc"), lRequest, nSize);
    return (TRUE);
}
*/

int real_main(int preinit)
{
    ShibTargetConfig& conf=ShibTargetConfig::getConfig();
    if (preinit) {

        // initialize the shib-target library
        conf.setFeatures(
            ShibTargetConfig::Listener |
            ShibTargetConfig::Caching |
            ShibTargetConfig::Metadata |
            ShibTargetConfig::Trust |
            ShibTargetConfig::Credentials |
            ShibTargetConfig::AAP |
            ShibTargetConfig::OutOfProcess |
            (shar_checkonly ? (ShibTargetConfig::InProcess | ShibTargetConfig::RequestMapper) : ShibTargetConfig::Logging)
            );
        if (!shar_config)
            shar_config=getenv("SHIBCONFIG");
        if (!shar_schemadir)
            shar_schemadir=getenv("SHIBSCHEMAS");
        if (!shar_schemadir)
            shar_schemadir=SHIB_SCHEMAS;
        if (!shar_config)
            shar_config=SHIB_CONFIG;
        if (!conf.init(shar_schemadir) || !conf.load(shar_config)) {
            fprintf(stderr, "configuration is invalid, see console for specific problems\n");
            return -2;
        }

        // If just a test run, bail.
        if (shar_checkonly) {
            fprintf(stdout, "overall configuration is loadable, check console for non-fatal problems\n");
            return 0;
        }
    }
    else {

        //_CrtSetAllocHook(MyAllocHook);

        // Run the listener
        if (!shar_checkonly) {

            // Run the listener.
            if (!conf.getINI()->getListener()->run(&shibd_shutdown)) {
                fprintf(stderr, "listener failed to enter listen loop\n");
                return -3;
            }
        }

        conf.shutdown();
    }
    return 0;
}

#else

static void term_handler(int arg)
{
    shibd_shutdown = true;
}

static int setup_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof (sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGPIPE, &sa, NULL) < 0) {
        return -1;
    }

    memset(&sa, 0, sizeof (sa));
    sa.sa_handler = term_handler;
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        return -1;
    }
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        return -1;
    }
    if (sigaction(SIGQUIT, &sa, NULL) < 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL) < 0) {
        return -1;
    }
    return 0;
}

static void usage(char* whoami)
{
    fprintf(stderr, "usage: %s [-fcdt]\n", whoami);
    fprintf(stderr, "  -c\tconfig file to use.\n");
    fprintf(stderr, "  -d\tschema directory to use.\n");
    fprintf(stderr, "  -t\tcheck configuration file for problems.\n");
    fprintf(stderr, "  -f\tforce removal of listener socket.\n");
    fprintf(stderr, "  -p\tpid file to use.\n");
    fprintf(stderr, "  -h\tprint this help message.\n");
    exit(1);
}

static int parse_args(int argc, char* argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "c:d:p:fth")) > 0) {
        switch (opt) {
            case 'c':
                shar_config=optarg;
                break;
            case 'd':
                shar_schemadir=optarg;
                break;
            case 'f':
                unlink_socket = 1;
                break;
            case 't':
                shar_checkonly=true;
                break;
            case 'p':
                pidfile=optarg;
                break;
            default:
                return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (setup_signals() != 0)
        return -1;

    if (parse_args(argc, argv) != 0)
        usage(argv[0]);

    if (!shar_config)
        shar_config=getenv("SHIBCONFIG");
    if (!shar_schemadir)
        shar_schemadir=getenv("SHIBSCHEMAS");
    if (!shar_schemadir)
        shar_schemadir=SHIB_SCHEMAS;
    if (!shar_config)
        shar_config=SHIB_CONFIG;

    // initialize the shib-target library
    ShibTargetConfig& conf=ShibTargetConfig::getConfig();
    conf.setFeatures(
        ShibTargetConfig::Listener |
        ShibTargetConfig::Caching |
        ShibTargetConfig::Metadata |
        ShibTargetConfig::Trust |
        ShibTargetConfig::Credentials |
        ShibTargetConfig::AAP |
        ShibTargetConfig::OutOfProcess |
        (shar_checkonly ? (ShibTargetConfig::InProcess | ShibTargetConfig::RequestMapper) : ShibTargetConfig::Logging)
        );
    if (!conf.init(shar_schemadir) || !conf.load(shar_config)) {
        fprintf(stderr, "configuration is invalid, check console for specific problems\n");
        return -2;
    }

    if (shar_checkonly)
        fprintf(stderr, "overall configuration is loadable, check console for non-fatal problems\n");
    else {

        // Write the pid file
        if (pidfile) {
            FILE* pidf = fopen(pidfile, "w");
            if (pidf) {
                fprintf(pidf, "%d\n", getpid());
                fclose(pidf);
            } else {
                perror(pidfile);  // keep running though
            }
        }
    
        // Run the listener
        if (!conf.getINI()->getListener()->run(&shibd_shutdown)) {
            fprintf(stderr, "listener failed to enter listen loop\n");
            return -3;
        }
    }

    conf.shutdown();
    if (pidfile)
        unlink(pidfile);
    return 0;
}

#endif
