#include <stdio.h>
#include <cmssys.h>
#include <signal.h>

int failed_tests=0;

void signal_handler(int signal) {
    printf("Received signal %d\n", signal);
    failed_tests--;
}

void fnExit1(void) {
    printf("Exit function 1\n");
}

void fnExit2(void) {
    printf("Exit function 2\n");
}

int main(int argc, char *argv[]) {

    printf("GCCLIB Sanity Test\n");

    printf("Immediate Flags Test\n");
    CMSSetFlag(TRACEFLAG, 0);
    CMSSetFlag(HALTFLAG, 0);
    printf("Trace %d\n", CMSGetFlag(TRACEFLAG));
    if (CMSGetFlag(TRACEFLAG) != 0) failed_tests++;
    printf("Halt %d\n", CMSGetFlag(HALTFLAG));
    if (CMSGetFlag(HALTFLAG) != 0) failed_tests++;

    CMSSetFlag(TRACEFLAG, 1);
    CMSSetFlag(HALTFLAG, 1);
    printf("Trace %d\n", CMSGetFlag(TRACEFLAG));
    if (CMSGetFlag(TRACEFLAG) != 1) failed_tests++;
    printf("Halt %d\n", CMSGetFlag(HALTFLAG));
    if (CMSGetFlag(HALTFLAG) != 1) failed_tests++;

    CMSSetFlag(TRACEFLAG, 0);
    CMSSetFlag(HALTFLAG, 1);
    printf("Trace %d\n", CMSGetFlag(TRACEFLAG));
    if (CMSGetFlag(TRACEFLAG) != 0) failed_tests++;
    printf("Halt %d\n", CMSGetFlag(HALTFLAG));
    if (CMSGetFlag(HALTFLAG) != 1) failed_tests++;

    CMSSetFlag(TRACEFLAG, 1);
    CMSSetFlag(HALTFLAG, 0);
    printf("Trace %d\n", CMSGetFlag(TRACEFLAG));
    if (CMSGetFlag(TRACEFLAG) != 1) failed_tests++;
    printf("Halt %d\n", CMSGetFlag(HALTFLAG));
    if (CMSGetFlag(HALTFLAG) != 0) failed_tests++;

    printf("Signal Test\n");
    failed_tests++;    /* Handler will reduce if run. */
    signal(SIGTERM, signal_handler);
    raise(SIGTERM);

    printf("Test Exit Functions (order 2,1)\n");
    atexit(fnExit1);
    atexit(fnExit2);

    return failed_tests;
}
