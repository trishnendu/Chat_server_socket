#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>
#include <sys/sem.h>

#include<sqlite3.h>

#define PORT 8080

struct sockaddr_in address;
sqlite3 *db; 

typedef struct tmp1{
	char name[20];
	int userid;
	int socket;
}userl;

userl active_users[128];
int active_user_cnt;

typedef struct tmp2{
	int src_userid;
	char src_name[20];
	char msg[1024];
}msgl;

msgl msg_cirqueue[1024];
int qin;
int qout;

int semgds;
int semcirque;


void sem_wait(int semgds,int num){
	struct sembuf buf={num,-1,0};
	semop(semgds,&buf,1);
}


void sem_signal(int semgds,int num){	
	struct sembuf buf={num,1,0};
	semop(semgds,&buf,1);
}


static int callback(void *data, int argc, char **argv, char **azColName){
   strcpy(data, argv[0]);
   return 0;
}

  
int get_slot(){
	int i;
	for(i = 0; i < active_user_cnt; i++){
		if(active_users[i].userid == -1)
			return i;
	}
	return active_user_cnt;
}
    
int setup_server(int *server_fd, const int opt, struct sockaddr_in *address){
	int attach;
	
	if((*server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0){
		return 1;
	}
	if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))){
		return 2;
	} 
	address->sin_family = AF_INET;
    address->sin_addr.s_addr = INADDR_ANY;
    address->sin_port = htons( PORT );
	if (bind(*server_fd, (struct sockaddr *)address,sizeof(*address))<0){
		return 3;
	}
    return 0;
}


int close_sock(const int sockid){
	if (shutdown(sockid, SHUT_RDWR) < 0){ 
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


int authenticate_client(const int new_socket,char buffer[]){
	int flag;
	
	char welcome[] = "\n~ Welcome to chat server 0.1 ~\n";
	char login[] = "\nChatserver login: ";
	char password[] = "Password: ";
	
	char err_user[] = "** No such user exists! Contact admin ): **\n\n";
	char err_pass[] = "** Password Missmatch!! **\n\n";	
	
	char sqlpref[] = "select pass from users where name like ";
	char sql[1024];
	char name[20];
	char pass[10];
	char *zErrMsg = 0;
	char dbpass[10];
	
	send(new_socket, login, strlen(login), 0);
	flag = read(new_socket, name, 1024);
	name[flag - 1 ] = '\0';
	
	sprintf(sql, "%s'%s'", sqlpref,name);
	sprintf(dbpass, "NULL");
	flag = sqlite3_exec(db, sql, callback, (void*)dbpass, &zErrMsg);
	
	if(strcmp(dbpass, "NULL")){
		send(new_socket, password, strlen(password), 0);
		flag = read(new_socket, pass, 1024);
		pass[flag - 1] = '\0';
		if(!strcmp(dbpass, pass)){
			send(new_socket, welcome, strlen(welcome), 0);
			strcpy(buffer, name);
			return 0;
		}else{
			send(new_socket, err_pass, strlen(err_pass), 0);
		}
	}else{
		send(new_socket, err_user, strlen(err_user), 0);
	}
	
	return 1;
	
}


int send_active_userl(const int sockid){
	char buf[1024];
	int i;
	
	sprintf(buf,"\n\nFollowing users are online! :)\n",active_user_cnt);
	for(i = 0; i < active_user_cnt; i++){
    	if(active_users[i].userid != -1){
    		sprintf(buf,"%s %s\n",buf, active_users[i].name);
    	}
    }
    sprintf(buf,"%s\n",buf);
    send(sockid, buf, strlen(buf), 0);
    return 0;
}


int notify_client_byname(const char *username, const msgl tmpmsg){
	int i;
	char buffer[1024];
	
	sprintf(buffer,"\t\t\t%s: %s",tmpmsg.src_name, tmpmsg.msg);
	
	if(strcmp(username,"broadcast")){
		for(i = 0; i < active_user_cnt; i++){
			if((active_users[i].userid != -1) && (active_users[i].userid != tmpmsg.src_userid) && !strcmp(username, active_users[i].name)){
				printf("Sending %s to %d, status = %d", tmpmsg.msg, active_users[i].userid, send(active_users[i].socket, buffer, strlen(buffer), 0));
				fflush(stdout);
			}
		}
	}else{
		for(i = 0; i < active_user_cnt; i++){
			if((active_users[i].userid != -1) && (active_users[i].userid != tmpmsg.src_userid)){
				printf("Sending broadcast %s to %d, status = %d", tmpmsg.msg, active_users[i].userid, send(active_users[i].socket, buffer, strlen(buffer), 0));
				fflush(stdout);
			}
		}
	}
	return 0;
}

void *tf_global_writer(void *var){
	int src, dest, i, j;
	char buffer[1024], dest_name[20];
	char tags[100][20];
	char *c;
	int flag = 0;
	msgl tmpmsg;
	
	while(1){
		sem_wait(semcirque, 2);
		sem_wait(semcirque, 0);
		strcpy(tmpmsg.msg, msg_cirqueue[qout].msg);
		strcpy(tmpmsg.src_name, msg_cirqueue[qout].src_name);
		tmpmsg.src_userid = msg_cirqueue[qout].src_userid;
		qout = (qout + 1) % 1024;
		sem_signal(semcirque, 0);
		sem_signal(semcirque, 1);

		c = tmpmsg.msg;
		flag = 0;
		
		for(i = 0; i < strlen(tmpmsg.msg); i++){
			if(tmpmsg.msg[i] == '@'){
				c = &tmpmsg.msg[i];
				while((tmpmsg.msg[i] != ' ') && (tmpmsg.msg[i] != '\n')){
					i++;
				}
				memset(dest_name, 0, 20);
				strncpy(dest_name, c + 1, &tmpmsg.msg[i] - c - 1);
				for(j = 0; j < flag ; j++){
					if(!strcmp(dest_name,tags[j]))
						break;
				}
				if(j == flag){
					notify_client_byname(dest_name, tmpmsg);
					strcpy(tags[flag], dest_name);
					flag++;
				}
			}
		}
		if(!flag){
			memset(dest_name, 0, 20);
			sprintf(dest_name, "broadcast");
			printf("broadcast detected! msg: %s\n",buffer);
			notify_client_byname(dest_name, tmpmsg);
		}
		
	}
}


void *tf_read_client(void *var){
	int sockid = (int) var;
	int flag, i;
	int curid;
	char buffer[1024], msg[1024], dest_name[20];
	char *c;
	
	while(authenticate_client(sockid,buffer));
	
	sem_wait(semgds,1);
	//curid = get_slot();
	curid = active_user_cnt++;
	sem_signal(semgds,1);
	
	strcpy(active_users[curid].name, buffer);
	active_users[curid].userid = curid;  
	active_users[curid].socket = sockid;
	
	printf("Starting reading thread for %d\n", curid);
    send_active_userl(sockid);
    
    while(1){	
		memset(buffer, 0, 1024);
		if(((flag = read(sockid, buffer, 1024)) <= 0) || (buffer[flag - 2] == 27)){
			if(flag = close_sock(sockid)){
					printf("Error in closing %d\n",flag);
			}
			printf("Closing reading thread for %d\n", curid);
			sem_wait(semgds, 1);
			active_users[curid].userid = -1;
			//active_user_cnt--;  
			sem_signal(semgds, 1);
			return NULL;
		}
		else if(buffer[0] == 'r' && buffer[1] == '\n'){
			send_active_userl(sockid);
		}
		else{
			sem_wait(semcirque, 1);
			sem_wait(semcirque, 0);
			msg_cirqueue[qin].src_userid = active_users[curid].userid;
			strcpy(msg_cirqueue[qin].src_name, active_users[curid].name);
			strcpy(msg_cirqueue[qin].msg, buffer);
			qin = (qin + 1) % 1024;
			sem_signal(semcirque, 0);
			sem_signal(semcirque, 2);
		}
		//printf("%s", buffer);
		//fflush(stdout);
	}	
}


int main(int argc, char const *argv[]){
    int server_fd, new_socket;
    int opt = 1;
    int flag;
    int addrlen = sizeof(address);
    char buffer[1024];
    char clntName[INET6_ADDRSTRLEN];
    pthread_t rtid, wtid;
   
   	semgds=semget(1234, 1, IPC_CREAT | 0666);
   	semctl(semgds, 0, SETVAL, 1);
   	
   	semcirque = semget(1235, 3, IPC_CREAT | 0666);
   	semctl(semcirque, 0, SETVAL, 1);
   	semctl(semcirque, 1, SETVAL, 1024);
   	semctl(semcirque, 2, SETVAL, 0);
   	 
    if(flag = setup_server(&server_fd, opt, &address)){
    	perror("setup error");
    	printf("Error code - %d\n",flag);		
    	return 0;
    }
    
    if(flag = sqlite3_open("usernames.db", &db)){
    	perror("database error");
    	return 0;
    }
    
    pthread_create(&wtid, NULL, tf_global_writer, NULL);
    
    while(1){
		if (listen(server_fd, 128) < 0){
			perror("listen error");
			exit(EXIT_FAILURE);
		}
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen))<0){
			perror("accept error");
			exit(EXIT_FAILURE);
		}
	
		printf("Opening connection\n");			
		pthread_create(&rtid, NULL, tf_read_client,(void *)new_socket);
    }
    
	semctl(semgds,IPC_RMID,0);
	semctl(semcirque,IPC_RMID,0);
    return 0;
}
