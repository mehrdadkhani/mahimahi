#include <sys/un.h>
#include <sys/socket.h>
#include <errno.h>
#include <iostream>
#include <string.h>
#include <unistd.h>
#include "util/event_loop.hh"

const char filename[] = "socket_file";

int main(int argc, char **argp, char **envp) {
    int err = 0;
    int s;
    s = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (s == -1) {
        printf("socket(2) returned error %d: %s\n", errno, strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    addr.sun_len = sizeof(addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, filename, sizeof(addr.sun_path));
    if (-1 == bind(s, (struct sockaddr *)&addr, sizeof(addr))) {
        printf("bind(2) returned error %d: %s\n", errno, strerror(errno));
        return -1;
    }

    if (-1 == listen(s, 1)) {
        printf("listen(2) returned error %d: %s\n", errno, strerror(errno));
        err = -1;
        goto clean_up;
    }

    while (1) {
        char client_addr[SOCK_MAXADDRLEN];
        socklen_t client_addr_len = sizeof(client_addr);
        int cs = accept(s, (struct sockaddr *)client_addr, &client_addr_len);
        if (cs == -1) {
            if (errno == ECONNABORTED)
                continue;
            if (errno == EINTR)
                continue;
            else {
                printf("accept(2) returned error %d: %s\n", errno, strerror(errno));
                err = -1;
                goto clean_up;
            }
        }

        if (client_addr_len > 0)
        {
            if (((struct sockaddr *)client_addr)->sa_family == AF_UNIX)
            {
                printf("Connection accepted from '%s'\n", ((struct sockaddr_un *)client_addr)->sun_path);
            }
            else
            {
                printf("Connection accepted from client with address len %d, address family %d\n",
                       ((struct sockaddr *)client_addr)->sa_len,
                       ((struct sockaddr *)client_addr)->sa_family);
            }
        }
        else
        {
            printf("Connection accepted from an unnamed client\n");
        }

        while (1) {
            char readbuffer[1024];
            ssize_t bytesread = recv(cs, readbuffer, sizeof(readbuffer), 0);
            if (bytesread == -1) {
                if (errno == ECONNRESET)
                    break;
                else if (errno == EINTR)
                    continue;
                else if (errno == ETIMEDOUT)
                    break;
                else {
                    printf("recv(2) returned error %d: %s\n", errno, strerror(errno));
                    err = -1;
                    close(cs);
                    goto clean_up;
                }
            }
            else if (bytesread == 0)
            {
                printf("Client disconnected\n");
                goto client_disconnected;
            }

            printf("Received %zd bytes:\n", bytesread);
            if (bytesread < 1023)
                readbuffer[bytesread] = '\0';
            else
                readbuffer[1023] = '\0';
            printf("%s\n", readbuffer);

            char sendbuffer[1024], *remaining = sendbuffer;
            strcpy(sendbuffer, "The time has come, the Walrus said, to Talk of Many Things");
            ssize_t bytestosend = strlen(sendbuffer);
            while (bytestosend > 0) {
                ssize_t bytessent = send(cs, remaining, bytestosend, 0);
                if (bytessent == -1) {
                    if (errno == ECONNRESET)
                    {
                        printf("Client disconnected\n");
                        goto client_disconnected;
                    }
                    else if (errno == EINTR) {
                        continue;
                    }
                    else {
                        printf("send(2) returned error %d: %s\n", errno, strerror(errno));
                        err = -1;
                        close(cs);
                        goto clean_up;
                    }
                }
                remaining += bytessent;
                bytestosend -= bytessent;
            }
        }
client_disconnected:

        close(cs);
    }

clean_up:
    close(s);
    unlink(filename);
    if (err == 0)
        printf("Success!\n");
    else
        printf("Error! %d\n", err);
    return err;
}
