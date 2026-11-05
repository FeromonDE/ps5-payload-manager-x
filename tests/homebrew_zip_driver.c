/* Thin wrapper: extract a .zip homebrew archive via the real backend code.
 * argv[1] = catalog filename (marker name), argv[2] = path to the .zip.
 * Prints "rc=<n>" and exits non-zero on extraction failure. The Python runner
 * builds the archives (safe + zip-slip) and asserts the resulting tree. */
#include <stdio.h>
#include <stdarg.h>
#include "slot.h"

void pldmgr_log(const char *fmt, ...) { (void)fmt; }

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    char msg[256] = {0};
    int rc = slot_install_homebrew_zip(argv[1], argv[2], msg, sizeof msg);
    printf("rc=%d msg=%s\n", rc, msg);
    return rc != 0;
}
