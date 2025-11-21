#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h> 
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <limits.h>		/* for OPEN_MAX */

#define PORT 5500   /* Port that will be opened */ 
#define BACKLOG 20   /* Number of allowed connections */
#define BUFF_SIZE 4096

/* The processData function copies the input string to output */
void processData(char *in, char *out);

/* The recv() wrapper function*/
int receiveData(int s, char *buff, int size, int flags);

/* The send() wrapper function*/
int sendData(int s, char *buff, int size, int flags);

int main()
{
	int					i, maxi, listenfd, connfd, sockfd;
	int					nready;
	ssize_t				n;
	char				buf[BUFF_SIZE];
	socklen_t			clilen;
	struct pollfd		client[OPEN_MAX];
	struct sockaddr_in	cliaddr, servaddr;

	l//Step 1: Construct a TCP socket to listen connection request
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1 ){  /* calls socket() */
		perror("socket() error: ");
		exit(EXIT_FAILURE);
	}

	//Step 2: Bind address to socket
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family      = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port        = htons(PORT);

	if(bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr))==-1){ /* calls bind() */
		perror("bind() error: ");
		exit(EXIT_FAILURE);
	} 

	//Step 3: Listen request from client
	if(listen(listenfd, BACKLOG) == -1){  /* calls listen() */
		perror("listen() error: ");
		exit(EXIT_FAILURE);
	}

	client[0].fd = listenfd;
	client[0].events = POLLRDNORM;
	for (i = 1; i < OPEN_MAX; i++)
		client[i].fd = -1;		/* -1 indicates available entry */
	maxi = 0;					/* max index into client[] array */
	
	//Step 4: Communicate with clients
	while (1) {
		nready = poll(client, maxi + 1, INFTIM);
		
		if(nready < 0){
			perror("poll() error: ");
			exit(EXIT_FAILURE);
		}
		
		if (client[0].revents & POLLRDNORM) {	/* new client connection */
			clilen = sizeof(cliaddr);
			if((connfd = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen)) < 0)
				perror("accept() error: ");
			else{
				printf("You got a connection from %s\n", inet_ntoa(cliaddr.sin_addr)); /* prints client's IP */

				for (i = 1; i < OPEN_MAX; i++)
					if (client[i].fd < 0) {
						client[i].fd = connfd;	/* save descriptor */
						break;
					}
				if (i == OPEN_MAX){
					printf("\nToo many clients");
					close(connfd);
				}
				else{
					client[i].events = POLLRDNORM;
					if (i > maxi)
						maxi = i;				/* max index in client[] array */
				}
				if (--nready <= 0)
					continue;				/* no more readable descriptors */
			}
		}

		for (i = 1; i <= maxi; i++) {	/* check all clients for data */
			if ((sockfd = client[i].fd) < 0)
				continue;
			if (client[i].revents & (POLLRDNORM | POLLERR)) {
				ret = receiveData(sockfd, rcvBuff, BUFF_SIZE, 0);
				if (ret <= 0){
					close(sockfd);
					client[i].fd = -1;
				}				
				else {
					processData(rcvBuff, sendBuff);
					ret = sendData(sockfd, sendBuff, ret, 0);
					if (ret < 0){
						close(sockfd);
						client[i].fd = -1;
					}
				}
				if (--nready <= 0)
					break;				/* no more readable descriptors */
			}
		}
	}
}

void processData(char *in, char *out){
	strcpy (out, in);
}

int receiveData(int s, char *buff, int size, int flags){
	int n;
	n = recv(s, buff, size, flags);
	if(n < 0)
		perror("recv() error: ");
	else if(n ==0 )
		printf("Connection closed!");
	return n;
}

int sendData(int s, char *buff, int size, int flags){
	int n;
	n = send(s, buff, size, flags);
	if(n < 0)
		perror("send() error: ");
	return n;
}