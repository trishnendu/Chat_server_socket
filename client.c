#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <pthread.h>
#define PORT 8080
#define BUFLEN 1024 

struct sockaddr_in serv_addr;
    
int setup_client(int *sockid){
	if ((*sockid = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("\n Socket creation error \n");
        return 1;
    }
  
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
      
    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0) {
        printf("\nInvalid address/ Address not supported \n");
        return 2;
    }
  
  	if (connect(*sockid, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
        printf("\nConnection Failed \n");
        return 3;
    }
    printf("Connected to server\n");
    
    return 0;
}

void *tf_read_sock(void *var){
	int sockid = (int) var;
	int valread, flag;
	char buffer[1024];
	//printf("Starting reading thread\n");
    
    while(1){	
		memset(buffer, 0, 1024);
		if((flag = read(sockid, buffer, 1024)) <= 0){
			printf("Closing reading thread\n");
			return NULL;
		}
		//printf("%d %c ", flag, buffer[flag - 2]);
		if(buffer[flag - 2] == 'r'){
			printf("Closing connection...\n");
			if(flag=close_sock(sockid)){
					printf("Error in closing %d\n",flag);
			}
			printf("Closing reading thread\n");
			return NULL;
		}
		printf("%s",buffer);
		fflush(stdout);
	}	
}

void *tf_write_sock(void *var){
	int sockid = (int) var;
	char *line = NULL;
    size_t n = 0;
    int flag;
    
    //printf("Starting writing thread\n");
    while(1){
		n = getline(&line, &n, stdin);
		if(line[n-2]==27){
			free(line);
			if(flag=close_sock(sockid)){
				printf("Error in closing %d\n",flag);
			}
			printf("Closing writing thread\n");
			return NULL;
		}
		if((flag = send(sockid , line , n , 0 )) <= 0){
			printf("Closing writing thread\n");
			return NULL;
		}
	}
}
  
int close_sock(const int sockid){
	if (shutdown(sockid, SHUT_RDWR) < 0){ // secondly, terminate the 'reliable' delivery
        perror("shutdown error");
    	return 1;    
    }
    if(close(sockid) < 0){
    	perror("close error");
    	return 2;     
	}
	printf("Connection closed\n");
	return 0;
}
 
int login_server(const int new_socket){
	char rbuffer[1024];
	char *wbuffer = NULL;
	size_t n = 0;
	printf("Trying to log in the server\n");
	read(new_socket, rbuffer, 1024);
	printf("%s", rbuffer);
	fflush(stdout);
	//read(new_socket, rbuffer, 1024);
	//printf("%s", rbuffer);
	//fflush(stdout);
	n = getline(&wbuffer, &n, stdin);
	send(new_socket, wbuffer, n, 0);
	read(new_socket, rbuffer, 1024);
	printf("%s", rbuffer);
	fflush(stdout);
	wbuffer = NULL;
	n = getline(&wbuffer, &n, stdin);
	send(new_socket, wbuffer, n, 0);
	
	return 1;
	
}
 
  
int main(int argc, char const *argv[]){
    int sock,flag;
    pthread_t rtid, wtid;
    if(flag=setup_client(&sock)){
    	printf("Error code %d\n",flag);
    	return 0;
    }
    
    //while(login_server(sock));
    pthread_create(&rtid, NULL, tf_read_sock,(void *)sock);
    pthread_create(&wtid, NULL, tf_write_sock,(void *)sock);
    pthread_join(wtid, NULL);
    pthread_join(rtid, NULL);
    
   
    //tf_write_sock((void *)sock);
    
    return 0;
}
