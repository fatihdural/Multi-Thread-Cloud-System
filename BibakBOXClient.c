#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <sys/inotify.h>
#include <pthread.h> 
#include <semaphore.h>      // gerekli library ler tanimlanir.
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
#define MAX_EVENTS 1024 
#define LEN_NAME 16 
#define EVENT_SIZE  ( sizeof (struct inotify_event) ) 
#define BUF_LEN     ( MAX_EVENTS * ( EVENT_SIZE + LEN_NAME )) 

int postOrderApply (char *name, char * toDir, int sock);        // klasorleri gezen gerekli klasorleri server'a yukleyen fonksiyon.
int postOrderApplyTraverse(char *name, char * toDir, int sock); // klasorleri bir degisikligi algilamak maksadiyla gezer.
void receiveMissingFiles(char *toDir, int new_socket, struct sockaddr_in address, int addrlen); // baglanti koptugunda silinmis dosyaları geri yukler.
void inotify(char *path, int sock);         // inotify, klasor icindeki degisikligi algilayan, recursive olmayan fonksiyondur.
void initializer(char *name, int size);     // stringleri initalize icin kullanilan fonksiyon.
char relativePath[255];

int main(int argc, char *argv[]) 
{ 
    struct sockaddr_in address;         // socket programlamada https://www.geeksforgeeks.org/socket-programming-cc/ sitesinden yararlanilmistir.
    int sock = 0; 
    struct sockaddr_in serv_addr; 
    int addrlen = sizeof(serv_addr);
    char calledPath[1024];
    strcpy(calledPath, argv[1]);
    if( strcmp(calledPath, ".") == 0 ){     // . icin current working directory alinir.
        char cwd[255];
        initializer(cwd, 255);
        getcwd(cwd, sizeof(cwd));
        strcpy(calledPath, cwd);
    }
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)       // socket olusturulur.
    { 
        printf("\n Socket creation error \n"); 
        return -1; 
    } 
    memset(&serv_addr, '0', sizeof(serv_addr)); 
    serv_addr.sin_family = AF_INET; 
    serv_addr.sin_port = htons(atoi(argv[3])); 
    if(inet_pton(AF_INET, argv[2], &serv_addr.sin_addr)<=0)  
    { 
        printf("\nInvalid address/ Address not supported \n"); 
        return -1; 
    } 
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)    // baglanti kurulur.
    { 
        printf("\nConnection Failed \n"); 
        return -1; 
    } 
    send(sock , calledPath , PATH_SIZE , 0 );       // calledPath in gonderilmesi.
    char last[255];
    strcpy(last, argv[1]);
    int i = 0, j = 0, count = 0, slashCount;
    while(last[i] != '\0'){
        if( last[i] == '/' && last[i+1] != '\0' ){        // klasör isminin bulunması, /home/gtucpp...../client -> client
            slashCount = i;
        }
        i++;
    }
    i = 0;
    char subDir[256] = {0};
    for( i = slashCount+1; i < 256; i++){
        if(last[i] != '/'){
            subDir[j] = last[i];
            j++;            
        }
        if(last[i] == '\0')
            break;
    }    
    strcpy(relativePath, subDir);    
    postOrderApply(argv[1], ".", sock);
    receiveMissingFiles(argv[1], sock, serv_addr, addrlen);
    if(fork()==0){                          // program akisinin kitlenmememesi maksadiyla child processler uretilir.
        postOrderApplyTraverse(last, ".", sock);
    }
    inotify(argv[1], sock);       // ana klasör için.
    while(wait(NULL)!=-1);
    return 0; 
} 

void receiveMissingFiles(char *toDir, int new_socket, struct sockaddr_in address, int addrlen){
    long int size = -3;     // sockete -3 gonderir, server kayip dosya istenimi oldugunu anlar.
    write(new_socket, &size, sizeof(long int));
    char *buffer = malloc(sizeof(int) * 254);
    int valread, count = 0;
    while(1){
        long int size;
        read( new_socket , &size, sizeof(long int));
        if( size == -5){    // kayip dosyalarin alinmasini bitiren flag.
            break;
        }
        initializer(buffer, 254);
        valread = read( new_socket , buffer, PATH_SIZE);
        if(valread <= 0)
            break;
        buffer = &buffer[1];
        char targetDir[254] = {0};
        initializer(targetDir, 254);
        strcpy(targetDir, toDir);
        strcat(targetDir, buffer);
        if( size == -1){                //klasör
            mkdir(targetDir, 0777);
        }
        else{                           // file
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

int postOrderApplyTraverse(char *name, char * toDir, int sock){
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
            char copyTarget[256];
            strcpy(copyTarget, targetDir);
            if(fork()==0){
                inotify(path_in, sock);     // her klasor icin degisiklilikleri inotify ile algilar.
            }
            else
                postOrderApplyTraverse(path_in, copyTarget, sock); // recursive cagri
        } 
        else            // klasor olmayanlar icin dongu bolumu
        {
            snprintf(path_in, sizeof(path_in), "%s/%s", name, entry->d_name);
        }
    }
    closedir(dir);      // directory i kapat.
    return size;  
}

int postOrderApply (char *name, char * toDir, int sock)
{
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
            postOrderApply(path_in, copyTarget, sock); // recursive cagri.
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

void inotify(char *argv, int sockett) { // https://www.thegeekstuff.com/2010/04/inotify-c-program-example/ sitesinden yararlanilmistir.
    int length, i = 0, wd;
    int fd;
    char buffer[BUF_LEN];
    while(1)    // surekli izle
    {
        fd = inotify_init();    // initialize.
        wd = inotify_add_watch(fd, argv, IN_CREATE | IN_MODIFY | IN_DELETE);    // 
        i = 0;
        length = read( fd, buffer, BUF_LEN );  
        while ( i < length ){
            struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
            if ( event->len ){      // degisimi algilar.
                if ( event->mask & IN_CREATE){
                    if ( ! (event->mask & IN_ISDIR) ){      // file ekleme, modifikasyon, silme islemlerine bakilacak.
                        char new[254];
                        initializer(new, 254);
                        int i = 0, j = 0, count = 0, slashCount;
                        while(argv[i] != '\0'){
                            if( argv[i] == '/' && argv[i+1] != '\0' ){        // klasör isminin bulunması, /home/gtucpp...../client -> client
                                slashCount = i;
                            }
                            i++;
                        }
                        i = 0;
                        char subDir[256] = {0};
                        for( i = slashCount+1; i < 256; i++){       // algoritmaya bagli string islemleri.
                            if(argv[i] != '/'){
                                subDir[j] = argv[i];
                                j++;            
                            }
                            if(argv[i] == '\0')
                                break;
                        }
                        strcpy(new, argv);
                        strcat(new, "/");
                        strcat(new, event->name);
                        struct stat fileStat;
                        lstat(new, &fileStat);
                        long int size = fileStat.st_size;
                        write(sockett, &size, sizeof(long int));
                        char *res = strstr(new, relativePath);
                        res = strstr(res, "/");
                        write(sockett, res, PATH_SIZE);   
                        int readfp = open(new, O_RDONLY, 0777);
                        while(1){
                            char readen[10];
                            initializer(readen, 10);
                            if( read(readfp, readen, 9) <= 0){
                                break;
                            }
                            write(sockett , readen , 9); 
                        }
                        close(readfp);                         
                    }
                }

                else if ( event->mask & IN_MODIFY){
                    if ( !(event->mask & IN_ISDIR) ){
                        char new[254];
                        initializer(new, 254);
                        int i = 0, j = 0, count = 0, slashCount;
                        while(argv[i] != '\0'){
                            if( argv[i] == '/' && argv[i+1] != '\0' ){        // klasör isminin bulunması, /home/gtucpp...../client -> client
                                slashCount = i;
                            }
                            i++;
                        }
                        i = 0;
                        char subDir[256] = {0};
                        for( i = slashCount+1; i < 256; i++){
                            if(argv[i] != '/'){
                                subDir[j] = argv[i];
                                j++;            
                            }
                            if(argv[i] == '\0')
                                break;
                        }
                        strcpy(new, argv);
                        strcat(new, "/");
                        strcat(new, event->name);
                        struct stat fileStat;
                        lstat(new, &fileStat);
                        long int size = fileStat.st_size;
                        write(sockett, &size, sizeof(long int));
                        char *res = strstr(new, relativePath);
                        res = strstr(res, "/");
                        write(sockett, res, PATH_SIZE);   
                        int readfp = open(new, O_RDONLY, 0777);
                        while(1){
                            char readen[10];
                            initializer(readen, 10);
                            if( read(readfp, readen, 9) <= 0){
                                break;
                            }
                            write(sockett , readen , 9); 
                        }
                        close(readfp);       
                    }
                }

                else if ( event->mask & IN_DELETE)
                {
                    if ( !(event->mask & IN_ISDIR) ){
                        char new[254];
                        initializer(new, 254);
                        int i = 0, j = 0, count = 0;
                        int slashCount;
                        while(argv[i] != '\0'){
                            if( argv[i] == '/' && argv[i+1] != '\0' ){        // klasör isminin bulunması, /home/gtucpp...../client -> client
                                slashCount = i;
                            }
                            i++;
                        }
                        i = 0;
                        char subDir[256] = {0};
                        for( i = slashCount+1; i < 256; i++){
                            if(argv[i] != '/'){
                                subDir[j] = argv[i];
                                j++;            
                            }
                            if(argv[i] == '\0')
                                break;
                        }
                        strcpy(new, argv);
                        strcat(new, "/");
                        strcat(new, event->name);
                        long int size = -2;     // silme flagi.
                        write(sockett, &size, sizeof(long int));
                        char *res = strstr(new, relativePath);
                        res = strstr(res, "/");
                        write(sockett, res, PATH_SIZE);   
                    }     
                }  
                i += EVENT_SIZE + event->len;
            }
        }
    }
    inotify_rm_watch( fd, wd );
    close( fd );
}

void initializer(char *name, int size){
    int i = 0;
    for( i = 0; i < size; i++){
        name[i] = '\0';
    }
}



