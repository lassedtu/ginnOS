/**
 * @file fetch.c
 * @brief fetch: get a file over HTTP/1.1 and write it out.
 *
 * usage: fetch [-i] [-I] [-o file] url
 *   -i       also write the response headers
 *   -I       send HEAD: only the headers
 *   -o file  write the body to file, not to standard output
 *
 * the url is http://host[:port]/path (http:// may be left out). bodies
 * with Content-Length, chunked bodies and bodies to the end of the
 * connection are read. redirects to http urls are followed, up to
 * MAX_REDIRECTS. https is not supported.
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_REDIRECTS 5
#define LINE_MAX_SIZE 4096
#define URL_MAX       2048

typedef struct
{
    char host[256];
    char port[8];
    char path[URL_MAX];
} url_t;

typedef struct
{
    int fd;
    char data[16384];
    size_t at;
    size_t length;
} reader_t;

typedef struct
{
    int status;
    char reason[128];
    long long content_length; // -1: not given
    bool chunked;
    char location[URL_MAX];
} response_t;

static bool show_headers;
static bool head_only;
static int out_fd = STDOUT_FILENO;
static long long body_bytes;

static void usage(void)
{
    fprintf(stderr, "usage: fetch [-i] [-I] [-o file] url\n");
    exit(2);
}

/**
 * split a url into its host, port and path.
 * @return false (with a message) if it is not an http url.
 */
static bool parse_url(const char *text, url_t *url)
{
    if (strncmp(text, "https://", 8) == 0)
    {
        fprintf(stderr, "fetch: https is not supported yet\n");
        return false;
    }
    if (strncmp(text, "http://", 7) == 0)
    {
        text += 7;
    }
    else if (strstr(text, "://"))
    {
        fprintf(stderr, "fetch: only http urls are supported\n");
        return false;
    }
    size_t host_length = strcspn(text, ":/?#");
    if (host_length == 0 || host_length >= sizeof(url->host))
    {
        fprintf(stderr, "fetch: the url has no host name\n");
        return false;
    }
    memcpy(url->host, text, host_length);
    url->host[host_length] = '\0';
    text += host_length;
    strcpy(url->port, "80");
    if (*text == ':')
    {
        text++;
        size_t port_length = strspn(text, "0123456789");
        if (port_length == 0 || port_length >= sizeof(url->port))
        {
            fprintf(stderr, "fetch: the port in the url is not valid\n");
            return false;
        }
        memcpy(url->port, text, port_length);
        url->port[port_length] = '\0';
        text += port_length;
    }
    size_t path_length = strcspn(text, "#");
    if (path_length + 2 > sizeof(url->path))
    {
        fprintf(stderr, "fetch: the url is too long\n");
        return false;
    }
    url->path[0] = '/';
    memcpy(url->path + (*text == '/' ? 0 : 1), text, path_length);
    url->path[path_length + (*text == '/' ? 0 : 1)] = '\0';
    return true;
}

/**
 * connect to the url's host, trying each of its addresses.
 * @return the socket, or -1 (with a message).
 */
static int open_connection(const url_t *url)
{
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *list;
    int err = getaddrinfo(url->host, url->port, &hints, &list);
    if (err != 0)
    {
        fprintf(stderr, "fetch: %s: %s\n", url->host, gai_strerror(err));
        return -1;
    }
    int fd = -1;
    int saved = 0;
    for (struct addrinfo *ai = list; ai && fd < 0; ai = ai->ai_next)
    {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen) != 0)
        {
            saved = errno;
            close(fd);
            fd = -1;
        }
    }
    freeaddrinfo(list);
    if (fd < 0)
    {
        fprintf(stderr, "fetch: cannot connect to %s port %s: %s\n", url->host, url->port,
                strerror(saved));
    }
    return fd;
}

static bool write_all(int fd, const char *data, size_t length)
{
    while (length > 0)
    {
        ssize_t n = write(fd, data, length);
        if (n < 0 && errno == EINTR)
        {
            continue;
        }
        if (n <= 0)
        {
            return false;
        }
        data += n;
        length -= (size_t)n;
    }
    return true;
}

/**
 * fill the reader's buffer when it is empty.
 * @return false at the end of the stream or on an error.
 */
static bool fill(reader_t *r)
{
    if (r->at < r->length)
    {
        return true;
    }
    ssize_t n;
    do
    {
        n = read(r->fd, r->data, sizeof(r->data));
    } while (n < 0 && errno == EINTR);
    if (n <= 0)
    {
        return false;
    }
    r->at = 0;
    r->length = (size_t)n;
    return true;
}

/**
 * read one line, without its CR LF.
 * @return false at the end of the stream.
 */
static bool read_line(reader_t *r, char *line, size_t size)
{
    size_t used = 0;
    while (fill(r))
    {
        char c = r->data[r->at++];
        if (c == '\n')
        {
            if (used > 0 && line[used - 1] == '\r')
            {
                used--;
            }
            line[used] = '\0';
            return true;
        }
        if (used + 1 < size)
        {
            line[used++] = c;
        }
    }
    return false;
}

/**
 * copy body bytes to the output: count of them, or all until the end of
 * the stream when count is -1.
 * @return false if the stream ended too early or the output failed.
 */
static bool copy_body(reader_t *r, long long count)
{
    while (count != 0)
    {
        if (!fill(r))
        {
            return count < 0;
        }
        size_t n = r->length - r->at;
        if (count > 0 && (long long)n > count)
        {
            n = (size_t)count;
        }
        if (!write_all(out_fd, r->data + r->at, n))
        {
            fprintf(stderr, "fetch: cannot write the output: %s\n", strerror(errno));
            exit(1);
        }
        r->at += n;
        body_bytes += (long long)n;
        if (count > 0)
        {
            count -= (long long)n;
        }
    }
    return true;
}

/**
 * a chunked body: chunks of hex size, then a 0 chunk and the trailers.
 */
static bool copy_chunked(reader_t *r)
{
    char line[LINE_MAX_SIZE];
    for (;;)
    {
        if (!read_line(r, line, sizeof(line)))
        {
            return false;
        }
        char *end;
        long long size = strtoll(line, &end, 16);
        if (end == line || size < 0)
        {
            return false;
        }
        if (size == 0)
        {
            break;
        }
        if (!copy_body(r, size) || !read_line(r, line, sizeof(line)))
        {
            return false;
        }
    }
    // the trailers, to the empty line
    while (read_line(r, line, sizeof(line)) && line[0] != '\0')
    {
    }
    return true;
}

/**
 * the value of a header line if it has the name, or NULL.
 */
static const char *header_value(const char *line, const char *name)
{
    size_t length = strlen(name);
    if (strncasecmp(line, name, length) != 0 || line[length] != ':')
    {
        return NULL;
    }
    line += length + 1;
    while (*line == ' ' || *line == '\t')
    {
        line++;
    }
    return line;
}

/**
 * read the status line and the headers.
 */
static bool read_response(reader_t *r, response_t *response)
{
    char line[LINE_MAX_SIZE];
    memset(response, 0, sizeof(*response));
    response->content_length = -1;
    do
    {
        if (!read_line(r, line, sizeof(line)))
        {
            fprintf(stderr, "fetch: the server closed the connection without an answer\n");
            return false;
        }
        int consumed = 0;
        if (sscanf(line, "HTTP/%*d.%*d %d %n", &response->status, &consumed) < 1)
        {
            fprintf(stderr, "fetch: the answer is not HTTP\n");
            return false;
        }
        snprintf(response->reason, sizeof(response->reason), "%s", line + consumed);
        if (show_headers)
        {
            printf("%s\r\n", line);
        }
        // the headers; a 1xx answer is followed by the real one
        while (read_line(r, line, sizeof(line)) && line[0] != '\0')
        {
            if (show_headers)
            {
                printf("%s\r\n", line);
            }
            const char *value;
            if ((value = header_value(line, "Content-Length")))
            {
                response->content_length = strtoll(value, NULL, 10);
            }
            else if ((value = header_value(line, "Transfer-Encoding")))
            {
                // chunked is the last coding when it is used
                size_t length = strlen(value);
                while (length > 0 && (value[length - 1] == ' ' || value[length - 1] == '\t'))
                {
                    length--;
                }
                response->chunked =
                    length >= 7 && strncasecmp(value + length - 7, "chunked", 7) == 0;
            }
            else if ((value = header_value(line, "Location")))
            {
                snprintf(response->location, sizeof(response->location), "%s", value);
            }
        }
        if (show_headers)
        {
            printf("\r\n");
            fflush(stdout);
        }
    } while (response->status >= 100 && response->status < 200);
    return true;
}

/**
 * the url a redirect goes to: the Location as it is, or a path on the
 * same host.
 */
static bool follow(url_t *url, const char *location)
{
    if (location[0] == '/' && location[1] != '/')
    {
        snprintf(url->path, sizeof(url->path), "%s", location);
        return true;
    }
    return parse_url(location, url);
}

int main(int argc, char **argv)
{
    const char *output = NULL;
    int opt;
    while ((opt = getopt(argc, argv, "iIo:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            show_headers = true;
            break;
        case 'I':
            head_only = true;
            show_headers = true;
            break;
        case 'o':
            output = optarg;
            break;
        default:
            usage();
        }
    }
    if (optind != argc - 1)
    {
        usage();
    }
    url_t url;
    if (!parse_url(argv[optind], &url))
    {
        return 1;
    }

    for (int redirects = 0;; redirects++)
    {
        int fd = open_connection(&url);
        if (fd < 0)
        {
            return 1;
        }
        bool default_port = strcmp(url.port, "80") == 0;
        char request[URL_MAX + 512];
        int length = snprintf(request, sizeof(request),
                              "%s %s HTTP/1.1\r\n"
                              "Host: %s%s%s\r\n"
                              "User-Agent: fetch/1.0 (ginnOS)\r\n"
                              "Accept: */*\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              head_only ? "HEAD" : "GET", url.path, url.host,
                              default_port ? "" : ":", default_port ? "" : url.port);
        if (!write_all(fd, request, (size_t)length))
        {
            fprintf(stderr, "fetch: cannot send the request: %s\n", strerror(errno));
            return 1;
        }

        static reader_t reader;
        reader.fd = fd;
        reader.at = 0;
        reader.length = 0;
        response_t response;
        if (!read_response(&reader, &response))
        {
            return 1;
        }
        bool redirect = response.status == 301 || response.status == 302 ||
                        response.status == 303 || response.status == 307 ||
                        response.status == 308;
        if (redirect && response.location[0] != '\0')
        {
            close(fd);
            if (redirects == MAX_REDIRECTS)
            {
                fprintf(stderr, "fetch: too many redirects\n");
                return 1;
            }
            if (!follow(&url, response.location))
            {
                return 1;
            }
            continue;
        }

        if (output && !head_only)
        {
            out_fd = open(output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (out_fd < 0)
            {
                fprintf(stderr, "fetch: %s: %s\n", output, strerror(errno));
                return 1;
            }
        }
        bool complete = true;
        bool no_body = head_only || response.status == 204 || response.status == 304;
        if (!no_body)
        {
            complete = response.chunked ? copy_chunked(&reader)
                                        : copy_body(&reader, response.content_length);
        }
        close(fd);
        if (output && !head_only)
        {
            close(out_fd);
            fprintf(stderr, "fetch: saved %lld bytes to %s\n", body_bytes, output);
        }
        if (!complete)
        {
            fprintf(stderr, "fetch: the connection closed before the end of the body\n");
            return 1;
        }
        if (response.status >= 400)
        {
            fprintf(stderr, "fetch: the server answered %d %s\n", response.status,
                    response.reason);
            return 1;
        }
        return 0;
    }
}
