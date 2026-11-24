#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>

#define PORT        8000
#define BACKLOG     32
#define BUFFER_SIZE 8192

static const char *root_dir = ".";

static const char *mime_type(const char *path)
{
    static const struct { const char *ext, *type; } types[] = {
        {".html", "text/html; charset=utf-8"},
        {".htm",  "text/html; charset=utf-8"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".txt",  "text/plain; charset=utf-8"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".svg",  "image/svg+xml"},
        {".ico",  "image/x-icon"},
        {".pdf",  "application/pdf"},
        {".xml",  "application/xml"},
    };
    const char *dot = strrchr(path, '.');
    if (dot) {
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++)
            if (strcasecmp(dot, types[i].ext) == 0)
                return types[i].type;
    }
    return "application/octet-stream";
}

static void send_text(int client, int status, const char *text, const char *body)
{
    char header[256];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: text/plain; charset=utf-8\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: close\r\n\r\n",
                     status, text, strlen(body));
    write(client, header, (size_t)n);
    write(client, body, strlen(body));
}

/* Decode %XX sequences in place */
static void url_decode(char *s)
{
    char *out = s;
    while (*s) {
        if (s[0] == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *out++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else {
            *out++ = *s++;
        }
    }
    *out = '\0';
}

static void serve_file(int client, const char *url_path, int head_only)
{
    char path[2048];
    snprintf(path, sizeof(path), "%s%s", root_dir, url_path);

    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t len = strlen(path);
        snprintf(path + len, sizeof(path) - len, "%sindex.html",
                 (len > 0 && path[len - 1] == '/') ? "" : "/");
    }

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        send_text(client, 404, "Not Found", "404 Not Found\n");
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        send_text(client, 403, "Forbidden", "403 Forbidden\n");
        return;
    }

    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %lld\r\n"
                     "Connection: close\r\n\r\n",
                     mime_type(path), (long long)st.st_size);
    write(client, header, (size_t)n);

    if (!head_only) {
        char buf[BUFFER_SIZE];
        size_t got;
        while ((got = fread(buf, 1, sizeof(buf), f)) > 0)
            if (write(client, buf, got) < 0)
                break;
    }
    fclose(f);
}

static void handle_client(int client)
{
    char req[BUFFER_SIZE];
    ssize_t received = read(client, req, sizeof(req) - 1);
    if (received <= 0)
        return;
    req[received] = '\0';

    char method[16], url[1024], version[16];
    if (sscanf(req, "%15s %1023s %15s", method, url, version) != 3) {
        send_text(client, 400, "Bad Request", "400 Bad Request\n");
        return;
    }

    int head_only = (strcmp(method, "HEAD") == 0);
    if (strcmp(method, "GET") != 0 && !head_only) {
        send_text(client, 405, "Method Not Allowed", "405 Method Not Allowed\n");
        return;
    }

    /* Strip query string / fragment, decode, and block path traversal */
    char *q = strpbrk(url, "?#");
    if (q) *q = '\0';
    url_decode(url);

    if (url[0] != '/' || strstr(url, "..")) {
        send_text(client, 403, "Forbidden", "403 Forbidden\n");
        return;
    }

    printf("%s %s\n", method, url);
    fflush(stdout);
    serve_file(client, url, head_only);
}

int main(int argc, char *argv[])
{
    if (argc > 1)
        root_dir = argv[1];

    struct stat st;
    if (stat(root_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Not a directory: %s\n", root_dir);
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);   /* auto-reap child processes */

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return EXIT_FAILURE; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return EXIT_FAILURE;
    }
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen");
        return EXIT_FAILURE;
    }

    printf("Serving '%s' on http://localhost:%d\n", root_dir, PORT);
    fflush(stdout);

    for (;;) {
        int client = accept(server_fd, NULL, NULL);
        if (client < 0) {
            perror("accept");
            continue;
        }
        /* One process per connection so idle browser sockets don't block others */
        if (fork() == 0) {
            close(server_fd);
            handle_client(client);
            close(client);
            _exit(0);
        }
        close(client);
    }
}
