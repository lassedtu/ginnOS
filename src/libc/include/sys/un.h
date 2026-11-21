#pragma once

#include <sys/socket.h>

/**
 * @file sys/un.h
 * @brief the address of a Unix domain socket.
 *
 * the layout is ABI and must match ksockaddr_un_t in
 * src/kernel/ipc/unix_socket.h. the names are kept in a kernel table, not
 * as files: any path works, and nothing shows up in the directory.
 */

struct sockaddr_un
{
    sa_family_t sun_family; // AF_UNIX
    char sun_path[108];     // the name, terminated
};
