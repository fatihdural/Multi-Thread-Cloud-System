#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <pthread.h> 
#include <semaphore.h>		// gerekli library ler tanimlanir.
#include <unistd.h>
#include <string.h> 
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>   
#include <sys/wait.h>
#include <syslog.h>
#include <fcntl.h>
#define PATH_SIZE 255

int sendMissingFiles(char *name, char * toDir, int sock);
void initializer(char *name, int size);

int main(int argc, char *argv[]) 				// socket programlamada https://www.geeksforgeeks.org/socket-programming-cc/ sitesinden yararlanilmistir.
{ 
	int server_fd, new_socket, valread = 1, opt = 1;
	struct sockaddr_in address; 
	int addrlen = sizeof(address); 
	char bufferx[256] = {0}; 
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
	{ 
		perror("socket failed"); 
		exit(EXIT_FAILURE); 
	} 
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 
												&opt, sizeof(opt))) 
	{ 
		perror("setsockopt"); 
		exit(EXIT_FAILURE); 
	} 
	address.sin_family = AF_INET; 
	address.sin_addr.s_addr = INADDR_ANY; 
	address.sin_port = htons( atoi(argv[3]) ); 
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address))<0) 
	{ 
		perror("bind failed"); 
		exit(EXIT_FAILURE); 
	} 
	while(1){
		if (listen(server_fd, atoi(argv[2])) < 0) 
		{ 
			perror("listen"); 
			exit(EXIT_FAILURE); 
		} 
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address, 
						(socklen_t*)&addrlen))<0) 
		{ 
			perror("accept"); 
			exit(EXIT_FAILURE); 
		}
		read(new_socket, bufferx, PATH_SIZE);
		printf("Socket baglandi %s\n", bufferx);
		if (getpeername(new_socket, (struct sockaddr *)&address, &addrlen) == -1) {
	      perror("getpeername() failed");
	      return -1;
	   	}
	  	printf("Socket baglandi. IP addressi: %s\n", inet_ntoa(address.sin_addr));
		int i = 0, j = 0, count = 0, slashCount;
		while(bufferx[i] != '\0'){
			if( bufferx[i] == '/' && bufferx[i+1] != '\0' ){		// klasör isminin bulunması, /home/gtucpp...../client -> client
				slashCount = i;
			}
			i++;
		}
		i = 0;
		char subDir[256] = {0};
		for( i = slashCount+1; i < 256; i++){
			if(bufferx[i] != '/'){
				subDir[j] = bufferx[i];
				j++;			
			}
			if(bufferx[i] == '\0')
				break;
		}
		mkdir(argv[1], 0777);
		char toDir[256] = {0};
		if( strstr(bufferx, "/") != NULL  ){
			char toDir[256] = "./";
		}
		strcat(toDir, argv[1]);
		strcat(toDir, "/");
		strcat(toDir, subDir);
		mkdir(toDir, 0777);
		strcat(toDir, "/");
		int nBytes = 0;
		while(1){
			long int size;
			read( new_socket , &size, sizeof(long int));
			if( size == -3){
				sendMissingFiles(toDir, ".", new_socket);
				long int size = -5;						// kayip dosya islemi bitti, cliente bildirilir.
            	write(new_socket, &size, sizeof(long int));
			}
			else{
				initializer(bufferx, 254);
				valread = read( new_socket , bufferx, PATH_SIZE);
				if(valread <= 0)
					break;

				char targetDir[254] = {0};
				initializer(targetDir, 254);
				strcpy(targetDir, toDir);
				strcat(targetDir, bufferx);
				if( size == -1){				//klasör
					mkdir(targetDir, 0777);
				}
				else{							// file
					if( size == -2){			// delete islemi icin bir flag.( size (-) olamayacagından size degerinin cakısma olasiligi yok.)
						remove(targetDir);
					}
					else{
						int writefp = open(targetDir, O_CREAT | O_TRUNC  | O_WRONLY, 0777);
						if( size != 0){
								count = 0;
								while(1){	
									char readen[10] = {0};
									int nBytes = recvfrom(new_socket, readen, 9, 0, (struct sockaddr*)&address, &addrlen); 
									count += nBytes;
									if(count >= size){
										write(writefp, readen, strlen(readen));
										break;
									}
									write(writefp, readen, 9);
								} 						
						}
						close(writefp);
					}
				}				
			}
		}
		printf("Socket %s kapandi.\n", toDir); 		
		close(new_socket);
	}
	return 0; 
} 

int sendMissingFiles(char *name, char * toDir, int sock){
    int size = 0, size_in = 0, temp = 0, flag_sym = 0;
    struct dirent *entry;
    DIR *dir;
    if (!(dir = opendir(name))){            // verilen directory acilamazsa hata ver. programdan cik.
        perror ("Failed to open directory");
        return -1;     
    }
    while ((entry = readdir(dir)) != NULL) { // genel dongu
        char path_in[1024];     // kalan path
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) // nokta ve iki nokta icin genel dongunun basina don.
            continue;
        if (entry->d_type == DT_DIR) {      // klasor icin recursive cagri.
            snprintf(path_in, sizeof(path_in), "%s/%s", name, entry->d_name);       // path_in e name ve entry->d_name concatenete ini ata.
            char targetDir[256];
            initializer(targetDir, 256);
            strcpy(targetDir, toDir);
            strcat(targetDir, "/");
            strcat(targetDir, entry->d_name);
            long int size = -1;
            write(sock, &size, sizeof(long int));
            write(sock, targetDir, PATH_SIZE);
            char copyTarget[256];
            strcpy(copyTarget, targetDir);
            sendMissingFiles(path_in, copyTarget, sock); // recursive cagri.
        } 
        else            // klasor olmayanlar icin dongu bolumu
        {
            snprintf(path_in, sizeof(path_in), "%s/%s", name, entry->d_name);
            struct stat fileStat;
            lstat(path_in, &fileStat);
            long int size = fileStat.st_size;
            write(sock, &size, sizeof(long int));
            if ( S_ISREG(fileStat.st_mode)){ // regular file ise size i dondur.
                char targetDir[256];
                initializer(targetDir, 256);
                strcpy(targetDir, toDir);
                strcat(targetDir, "/");
                strcat(targetDir, entry->d_name);           
                char fileName[10];
                initializer(fileName, 10);
                strcpy(fileName, entry->d_name);
                write(sock, targetDir, PATH_SIZE);
                int readfp = open(path_in, O_RDONLY, 0777);
                while(1){
                    char readen[10];
                    initializer(readen, 10);
                    if( read(readfp, readen, 9) <= 0){
                        break;
                    }
                    write(sock , readen , 9); 
                }
                close(readfp);
            }
        }
    }
    closedir(dir);      // directory i kapat.
    return size; 	
}

void initializer(char *name, int size){
    int i = 0;
    for( i = 0; i < size; i++){
        name[i] = '\0';
    }
}
