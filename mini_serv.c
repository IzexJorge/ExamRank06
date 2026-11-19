
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

int		sockfd, maxfd, next_id;
fd_set	all, pending_out;

int		ids[FD_SETSIZE];
char	*inbuf[FD_SETSIZE];
char	*outbuf[FD_SETSIZE];

char	recv_buf[4096];
char	broadcast_buf[64];

int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}

void fatal_error()
{
	write(2, "Fatal error\n", 12);
	exit(1);
}

void clean_client(int fd)
{
	free(inbuf[fd]);
	free(outbuf[fd]);
	close(fd);
}

void clean_all()
{
	for (int fd = 0; fd <= maxfd; fd++)
	{
		if (FD_ISSET(fd, &all) && fd != sockfd)
			clean_client(fd);
	}
	close(sockfd);
}

void broadcast(int protagonist, char *msg)
{
	for (int fd = 0; fd <= maxfd; fd++)
	{
		if (FD_ISSET(fd, &all) && fd != sockfd && fd != protagonist)
		{
			outbuf[fd] = str_join(outbuf[fd], msg);
			if (outbuf[fd] == 0)
			{
				clean_all();
				fatal_error();
			}

			FD_SET(fd, &pending_out);
		}
	}
}

void remove_client(int fd)
{
	FD_CLR(fd, &all);
	FD_CLR(fd, &pending_out);
	clean_client(fd);
	sprintf(broadcast_buf, "server: client %d just left\n", ids[fd]);
	broadcast(fd, broadcast_buf);
}

void send_out(int fd)
{
	ssize_t	bytes;
	char	*rest;

	bytes = send(fd, outbuf[fd], strlen(outbuf[fd]), MSG_DONTWAIT | MSG_NOSIGNAL);
	if (bytes <= 0)
		return;

	rest = str_join(0, outbuf[fd] + bytes);
	if (rest == 0)
	{
		clean_all();
		fatal_error();
	}

	free(outbuf[fd]);
	outbuf[fd] = rest;

	if (rest[0] == 0)
		FD_CLR(fd, &pending_out);
}

void run()
{
	fd_set 				readfds, writefds;
	int 				connfd;
	socklen_t 			len;
	struct sockaddr_in	cli;
	ssize_t 			bytes;
	char				*msg;
	int 				extract_status;

	while (1)
	{
		readfds = all;
		writefds = pending_out;

		if (select(maxfd + 1, &readfds, &writefds, 0, 0) <= 0)
			continue;

		for (int fd = 0; fd <= maxfd; fd++)
		{
			if (FD_ISSET(fd, &writefds))
				send_out(fd);

			if (!FD_ISSET(fd, &readfds))
				continue;

			if (fd == sockfd)
			{
				len = sizeof(cli);
				connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
				if (connfd < 0)
					continue;
				if (connfd >= FD_SETSIZE)
				{
					close(connfd);
					continue;
				}

				sprintf(broadcast_buf, "server: client %d just arrived\n", next_id);
				broadcast(connfd, broadcast_buf);

				ids[connfd] = next_id++;
				inbuf[connfd] = NULL;
				outbuf[connfd] = NULL;

				FD_SET(connfd, &all);
				if (connfd > maxfd)
					maxfd = connfd;
			}
			else
			{
				bytes = recv(fd, recv_buf, sizeof(recv_buf) - 1, 0);
				if (bytes <= 0)
				{
					remove_client(fd);
					continue;
				}	
				recv_buf[bytes] = 0;

				inbuf[fd] = str_join(inbuf[fd], recv_buf);
				if (inbuf[fd] == 0)
				{
					clean_all();
					fatal_error();
				}

				while ((extract_status = extract_message(&inbuf[fd], &msg)) == 1)
				{
					sprintf(broadcast_buf, "client %d: ", ids[fd]);
					broadcast(fd, broadcast_buf);
					broadcast(fd, msg);
					free(msg);
				}
				if (extract_status == -1)
				{
					clean_all();
					fatal_error();
				}
			}
		}
	}
}

int main(int argc, char *argv[])
{
	struct sockaddr_in servaddr;

	if (argc != 2)
	{
		write(2, "Wrong number of arguments\n", 26);
		exit(1);
	}

	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1)
		fatal_error();

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(atoi(argv[1]));

	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0)
		fatal_error();

	if (listen(sockfd, 4096) != 0)
		fatal_error();

	FD_ZERO(&all);
	FD_ZERO(&pending_out);

	FD_SET(sockfd, &all);
	maxfd = sockfd;

	run();
}
