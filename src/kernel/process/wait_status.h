#pragma once

/**
 * the status word waitpid reports for a child, encoded as on other Unix
 * systems (the libc's <sys/wait.h> decodes it):
 *   exited with code c:      (c & 0xff) << 8
 *   killed by signal s:      s (bits 0-6)
 */

#define WAIT_STATUS_EXITED(code) (((code) & 0xff) << 8)
#define WAIT_STATUS_SIGNALED(sig) ((sig) & 0x7f)

// waitpid options
#define WNOHANG 1 // return 0 at once if no child has exited
