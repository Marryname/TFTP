1	#include <stdlib.h>
2	#include <stdio.h>
3	#include <errno.h>
4	#include <unistd.h>
5	#include <string.h>
6	#include <signal.h>
7	#include <pthread.h>
8	#include <sys/time.h>
9	#include <sys/types.h>
10	#include <sys/socket.h>
11	#include <arpa/inet.h>
12
13	#define PORT 20000
14	#define BACKLOG 5
15	#define BUFLEN 256
16
17	void server (void *arg)
18	{
19		struct sockaddr_in caddr;
20		char msg [] = "Connect success. Please send a message to me.\n";
21		char buf [BUFLEN];
22		int len;
23		int c_s;
24		c_s = *((int *) arg);
25		pthread_detach (pthread_self());
26		send (c_s, msg, sizeof (msg), 0);
27		while (1)
28		{
29			memset (buf, 0, BUFLEN);
30			len = (int) recv (c_s, buf, sizeof (buf)-1, 0);
31			if (len <= 0)
32				break;
33			printf ("I receive a message: %s\n", buf);
34			send (c_s, buf, strlen (buf), 0);
35		}
36		pthread_exit (0);
37	}
38	void watchcmd (void *arg)
39	{
40		char cmd [10];
41		int l_s;
42		l_s = *((int *) arg);
43		while (strcmp (cmd, "exit"))
44			scanf ("%s", cmd);
45		close (l_s);
46		exit (0);
47	}
48	int main (void)
49	{
50		int lstsock;
51		int c_sock;
52		int addrlen;
53		int i;
54		pthread_t thread;
55		struct sockaddr_in saddr;
56		struct sockaddr_in caddr;
57		addrlen = sizeof (saddr);
58		if ((lstsock = socket (PF_INET, SOCK_STREAM, 0)) == -1)
59		{
60			fprintf (stderr, "Create socket error: %s\n", strerror (errno));
61			exit (1);
62		}
63		memset (&saddr, 0, addrlen);
64		saddr.sin_family = AF_INET;
65		saddr.sin_port = htons (PORT);
66		saddr.sin_addr.s_addr = htonl (INADDR_ANY);
67		if (bind (lstsock, (struct sockaddr*) &saddr, addrlen) == -1)
68		{
69			fprintf (stderr, "Bind socket error: %s\n", strerror (errno));
70			close (lstsock);
71			exit (1);
72		}
73		listen (lstsock, BACKLOG);
74		pthread_create (&thread, NULL, (void *) (&watchcmd), &lstsock);
75		while (1)
76		{
77			addrlen = sizeof (caddr);
78			c_sock = accept (lstsock, (struct sockaddr *) &caddr, &addrlen);
79			if (c_sock <= 0)
80				continue;
81			printf ("Got connection from: %s: %d\n", inet_ntoa (caddr.sin_addr), ntohs (caddr.sin_port));
82			pthread_create (&thread, NULL, (void *) &server, (void *) &c_sock);
83		}
84	}
