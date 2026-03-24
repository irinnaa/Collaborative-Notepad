//CollaborativeNotepad server
// Compilare: gcc server.c -o server -lsqlite3 -pthread
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sqlite3.h>

#define PORT 6060
#define BUFFER_SIZE 1024
#define MAX_FILES 10
#define MAX_USERS 10
#define MAX_CONTENT 10000 



sqlite3 *database;

struct User{ 
    char username[50];
    int  is_on;
    int socket;
};

struct File{
    char filename[50];
    char continut[MAX_CONTENT];
    int exists;
    int active_users;
    int editor_sockets[2];
    int editor_cursors[2];
    char editor_users[2][50];
    pthread_mutex_t lock;

};

struct User users[MAX_USERS]; 
struct File files[MAX_FILES];

pthread_mutex_t files_mutex;
pthread_mutex_t db_mutex;

extern int errno;

typedef struct thData{
    int id_thread;
    int cl; //socket for client
}thData;


static void *treat(void *);
// format of messages: "length:option parameters"


//  FUNCTII PENTRU BAZA DE DATE
void initializareDB() { 
    pthread_mutex_init(&files_mutex,NULL);
    pthread_mutex_init(&db_mutex,NULL);

    for(int i=0;i<MAX_FILES;i++)
    {
        files[i].exists=0;
        files[i].active_users=0;
        files[i].editor_sockets[0]=0;
        files[i].editor_sockets[1]=0;
        files[i].editor_cursors[0]=-1;
        files[i].editor_cursors[1]=-1;
        files[i].editor_users[0][0]='\0';
        files[i].editor_users[1][0]='\0';
        files[i].filename[0]='\0';
        files[i].continut[0]='\0';
        pthread_mutex_init(&files[i].lock,NULL);
    }

    int ret=sqlite3_open("collaborative_notepad.db",&database);

    if(ret)
    {
        fprintf(stderr,"[Server][Database] Eroare la deschiderea bazei de date: %s\n",sqlite3_errmsg(database));
    }else{

        printf("[Server][Database] Baza de date a fost deschisa\n");

    }

    //crearea tabel
    char *sql1="CREATE TABLE IF NOT EXISTS USERS("\
     "USERNAME TEXT PRIMARY KEY NOT NULL," \
      "PASSWORD TEXT);";

    char *erorMsg=0;
    ret=sqlite3_exec(database,sql1,0,0,&erorMsg); 

    if(ret!=SQLITE_OK){
        fprintf(stderr,"[Server][Database] Eroare SQL:%s\n",erorMsg);
        sqlite3_free(erorMsg);
    }else
    {
        printf("[Server][Database] Tabela USERS creata/verificata cu succes\n");
    }

    char *sql2="CREATE TABLE IF NOT EXISTS FILES("\
     "FILENAME TEXT PRIMARY KEY NOT NULL," \
      "CONTENT TEXT);";

     ret=sqlite3_exec(database,sql2,0,0,&erorMsg); 

    if(ret!=SQLITE_OK){
        fprintf(stderr,"[Server][Database] Eroare SQL:%s\n",erorMsg);
        sqlite3_free(erorMsg);
    }else{
        printf("[Server][Database] Tabela FILES creata/verificata cu succes\n");
    }


}

int loadFiles(void *notused, int argc, char **argv, char **azColName){

    pthread_mutex_lock(&files_mutex);
    int index=-1;
    for(int i=0;i<MAX_FILES;i++){
        if(!files[i].exists && index==-1)
        index=i;
    }

    if(index!=-1){
        files[index].exists=1;
        files[index].active_users=0;

        files[index].editor_sockets[0]=0; 
        files[index].editor_sockets[1]=0;

        files[index].editor_cursors[0]=-1; 
        files[index].editor_cursors[1]=-1;
        files[index].editor_users[0][0]='\0';
        files[index].editor_users[1][0]='\0';
        if(argv[0]){
        strcpy(files[index].filename, argv[0]);
        }else{
        strcpy(files[index].filename, "NULL");
        }

        if(argv[1])
        {
            strcpy(files[index].continut, argv[1]);
            
        }else{
            strcpy(files[index].continut, "");
        }

         printf("[Server][Database] S-a incarcat continutul din baza de date %s\n",files[index].filename);
    }

    pthread_mutex_unlock(&files_mutex);
    return 0;
}


struct FileListBuffer{
    char *buf;
    size_t cap;
};

int files_list(void *data, int argc,char **argv, char **axColName){
    struct FileListBuffer *list=(struct FileListBuffer*)data;
    if(!list || !list->buf) return 0;
    if(argv[0]){

        size_t used=strlen(list->buf);
        size_t ramas=(used<list->cap) ? list->cap-used : 0;

    if(ramas > 1){
    int written=snprintf(list->buf + used, ramas, "%s\n", argv[0]);
    if(written<0){
        list->buf[used]='\0';
    }
}
}
    return 0;
}

 int loadUsers(void *notused, int argc,char **argv, char **axColName) 
 {
    if(argv[0])        
        printf("[Server][Database] User inregistrat gasit: %s\n",argv[0]);
    
    return 0;
 }

void loadFromDB(){
    char *errorMsg=0;
// retrieve information from FILES
    char *sqlFiles="SELECT * FROM FILES;";
    pthread_mutex_lock(&db_mutex);
    int ret=sqlite3_exec(database,sqlFiles,loadFiles,0,&errorMsg);

if(ret!=SQLITE_OK)
    {  fprintf(stderr,"[Server][Database] Eroare la incarcarea fisierelor %s\n",errorMsg);
        sqlite3_free(errorMsg);
    }else
    {
        printf("[Server][Database] Fisiere incarcate in memoria serverului\n");
    }
//retrieve infromation from users

    char *sqlUsers="SELECT * FROM USERS;";
    ret=sqlite3_exec(database,sqlUsers,loadUsers,0,&errorMsg);

    if(ret!=SQLITE_OK)
    {
        fprintf(stderr,"[Server][Database] Eroare la incarcarea utilizatorilor %s\n",errorMsg);
        sqlite3_free(errorMsg);
    }
    pthread_mutex_unlock(&db_mutex);

}

// return: 0=succes, 1=user existent, 2=eroare sql
int registerUserBD(char *username, char *parola)
{
    int result=0;
    sqlite3_stmt *stmt=NULL;
    const char *sql="SELECT USERNAME FROM USERS WHERE USERNAME=?";

    pthread_mutex_lock(&db_mutex);
    if( sqlite3_prepare_v2(database,sql, -1,&stmt,0)!=SQLITE_OK)
    {
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }
    sqlite3_bind_text(stmt,1 ,username,-1,SQLITE_STATIC);

    int ret = sqlite3_step(stmt);
    int userExistent =(ret==SQLITE_ROW);
    sqlite3_finalize(stmt);
    stmt=NULL;

    if(userExistent)
    {

        pthread_mutex_unlock(&db_mutex);
        return 1;
    }

    const char *insertSQL="INSERT INTO USERS (USERNAME, PASSWORD) VALUES (?, ?);";
    
    ret=sqlite3_prepare_v2(database,insertSQL,-1,&stmt,0);
if(ret!=SQLITE_OK){
    pthread_mutex_unlock(&db_mutex);
    return 2;
}
    sqlite3_bind_text(stmt,1,username,-1,SQLITE_STATIC);
    sqlite3_bind_text(stmt,2,parola,-1,SQLITE_STATIC);
    ret=sqlite3_step(stmt);
    if(ret!=SQLITE_DONE)
    {
        fprintf(stderr,"[Server][Database] Eroare la inregistrare user %s\n",sqlite3_errmsg(database));
        result=2;
    }else{
        result=0;
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
    return result;
}
// 0:succes user si parola, 1: user nu e in bd, 2: user existent, parola gresita, 3: eroare sql

int checkLoginBD(char *username, char *parola){
    sqlite3_stmt *stmt;
    char *sql ="SELECT PASSWORD FROM USERS WHERE USERNAME=?";

    pthread_mutex_lock(&db_mutex);
    if(sqlite3_prepare_v2(database,sql,-1,&stmt,0)!=SQLITE_OK){
        pthread_mutex_unlock(&db_mutex);
        return 3;
    }
    sqlite3_bind_text(stmt,1,username,-1,SQLITE_STATIC);

    int ret = sqlite3_step(stmt);
   // int success=0;
if(ret==SQLITE_ROW){
    const char *parolaBD=(const char*)sqlite3_column_text(stmt,0);
    if(parolaBD && strcmp(parolaBD,parola)==0){
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 0;
    }else{
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 2;
    }
}else{
        sqlite3_finalize(stmt);
        pthread_mutex_unlock(&db_mutex);
        return 1;
    }  

}

void saveFileDB(char *filename, char *continut){

    sqlite3_stmt *stmt=NULL;
    const char *sql="INSERT OR REPLACE INTO FILES (FILENAME, CONTENT) VALUES (?, ?);";
    pthread_mutex_lock(&db_mutex);

    int ret=sqlite3_prepare_v2(database,sql,-1,&stmt,NULL);
    if(ret!=SQLITE_OK){
        fprintf(stderr,"[Server][Database] Eroare la pregatirea salvarii: %s\n",sqlite3_errmsg(database));
        pthread_mutex_unlock(&db_mutex);
        return;
    }

    sqlite3_bind_text(stmt,1,filename,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt,2,continut,-1,SQLITE_TRANSIENT);
    ret=sqlite3_step(stmt);
    if(ret!=SQLITE_DONE)
    {
        fprintf(stderr,"[Server][Database] Eroare la salvare: %s\n",sqlite3_errmsg(database));
    }else
    {
        printf("[Server][Database] Fisierul %s a fost salvat in baza de date\n",filename);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);

}

void deleteFileDB(char *filename){
    sqlite3_stmt *stmt=NULL;
    const char *sql="DELETE FROM FILES WHERE FILENAME=?;";
    pthread_mutex_lock(&db_mutex);
    int ret=sqlite3_prepare_v2(database,sql,-1,&stmt,NULL);
    if(ret!=SQLITE_OK){
        fprintf(stderr,"[Server][Database] Eroare la pregatirea stergerii: %s\n",sqlite3_errmsg(database));
        pthread_mutex_unlock(&db_mutex);
        return;
    }
    sqlite3_bind_text(stmt,1,filename,-1,SQLITE_TRANSIENT);
    ret=sqlite3_step(stmt);
    if(ret!=SQLITE_DONE){
        fprintf(stderr,"[Server][Database] Eroare la stergerea fisierului: %s\n",sqlite3_errmsg(database));
    }else{
        fprintf(stderr,"[Server][Database] Fisierul %s a fost sters de pe disc\n",filename);
    }
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&db_mutex);
}
//end baze de date

//functii pentru optiunile programului

void inserting(struct File *f, int pos, char *text)
{
    int current_len=strlen(f->continut);
    int text_len=strlen(text);

    if(current_len + text_len >=MAX_CONTENT)return;
    if(pos>current_len)
    pos=current_len;

    memmove(f->continut+pos+text_len, f->continut + pos, current_len-pos+1);

    memcpy(f->continut+pos,text,text_len);
}

void deleting(struct File *f, int pos, int len){
    int current_len=strlen(f->continut);
    if(len<0){
        len=-len;
        pos=pos-len;
    }
    if(pos<0) pos=0;
    if(pos>=current_len) return;
    if(pos+len>current_len) len=current_len-pos;

    memmove(f->continut+pos, f->continut +pos+len, current_len-pos-len+1);
}

int validareParola(const char *parola){
    if(strlen(parola)<8)
    return 0;

    int Upper=0;
    int lower=0;
    int digit=0;

    for(int i=0;parola[i]!='\0';i++){
        if(isupper(parola[i]))
        Upper=1;

        if(islower(parola[i]))
        lower=1;

        if(isdigit(parola[i]))
        digit=1;
    }
    return(Upper && lower && digit);
}
   

void itoa(int n,char s[],int base){
    int i=0;
    int sign=n;
    if(n<0)
    n=-n;

    do{
        int r=n%base;
        s[i++]=(r<10)?r+'0':r-10+'A';
    }while((n/=base)>0);

    if(sign<0)s[i++]='-';
    s[i]='\0';

    int k=i-1;
    int j=0;
    while(j<k){
        char aux=s[j];
        s[j]=s[k];
        s[k]=aux;
        j++;
        k--;
    }
}


void sendMess(int socket, const char *message){
    if(message==NULL)return;
    // length:messsage formatting
    int bytes=strlen(message); //message length

    char bytes_formatted[50];
    itoa(bytes,bytes_formatted,10);

    int header_l=strlen(bytes_formatted)+1;
    int total_allocated=header_l+bytes+1;

    char *packet=(char*)malloc(total_allocated*sizeof(char));
    if(packet==NULL){
        perror("[Server] Eroare la alocarea memoriei in sendMess\n");
        return;
    }

    packet[0]='\0';
    
    strcat(packet,bytes_formatted);
    strcat(packet,":");
    strcat(packet,message);

    if(send(socket,packet,strlen(packet),0)<0)
    {
         perror("[Server] Eroare la trimiterea mesajului!\n");
    }
    free(packet);
}

void broadcasting_edits(struct File *f, char *text){
    for(int i=0;i<2;i++){
        if(f->editor_sockets[i]!=0)
        sendMess(f->editor_sockets[i],text);
    }
}
void broadcatsing_update(struct File *f){
    int c1=(f->editor_sockets[0]!=0) ? f->editor_cursors[0]:-1;
    int c2=(f->editor_sockets[1]!=0) ? f->editor_cursors[1] : -1;

    int size=strlen(f->continut) + 50; 
    char *message=(char*)malloc(size);
    if(!message) 
    return;

    sprintf(message,"UPDATE:%d:%d:%s",c1, c2, f->continut);

    for(int i=0;i<2;i++)
    {
        if(f->editor_sockets[i]!=0)
        sendMess(f->editor_sockets[i],message);
    }
    free(message);
}

static int editor_index_socket(struct File *f,int socket_client){
    for(int i=0;i<2;i++){
        if(f->editor_sockets[i] == socket_client)
         return i;
    }
    return -1;
}

static void send_update_to_file(struct File *f){
    int size =strlen(f->continut)+50;
    for(int i=0;i<2;i++)
    {
        if(f->editor_sockets[i]==0) continue;

        int local_cursor=f->editor_cursors[i];
        int remote_cursor=-1;
        if(f->editor_sockets[1-i] != 0){
            remote_cursor=f->editor_cursors[1-i];
        }

        char *message=(char*)malloc(size);
        if(!message) continue;

        sprintf(message, "UPDATE:%d:%d:%s", local_cursor, remote_cursor, f->continut);
        sendMess(f->editor_sockets[i], message);
        free(message);
    }
}

static void lista_utilizatori(struct File *f, char *buffer, size_t cap){
    if(!buffer || cap==0)
     return;
    buffer[0]='\0';
    size_t used=0;

    for(int i=0;i<2;i++){
        if(f->editor_sockets[i]==0)
         continue;
        if(f->editor_users[i][0]=='\0')
         continue;

        if(used > 0 && used < cap - 1){
            int written=snprintf(buffer+used, cap-used, ", ");
            if(written < 0) break;
            used+=(size_t)written;
        }

        if(used < cap - 1){
            int written=snprintf(buffer+used, cap-used, "%s", f->editor_users[i]);
            if(written < 0) break;
            used+=(size_t)written;
    }
    }
}

static void send_fileinfo_to_file(struct File *f){
    char users[128];
    lista_utilizatori(f, users, sizeof(users));

    int size = strlen(f->filename) + strlen(users) + 20;
    for(int i=0;i<2;i++){
        if(f->editor_sockets[i]==0) continue;
        char *message = (char*)malloc(size);
        if(!message) continue;
        sprintf(message, "FILEINFO:%s:%s", f->filename, users);
        sendMess(f->editor_sockets[i], message);
        free(message);
    }
}

static void delete_helper(const char *content, int pos, int len, int *out_start, int *out_len){
    int current_len=strlen(content);
    int start=pos;
    int length=len;

    if(length<0){
        length=-length;
        start=pos- length;
    }
    if(start<0) start=0;
    if(start>current_len) 
    start=current_len;

    if(start+length>current_len) length=current_len-start;
    if(length<0) length=0;

    *out_start=start;
    *out_len=length;
}

int receiveMess(int socket,char *buffer){
    char len_str[20];
    int i=0;
    char c;
    int n;
    while(1){
        n=recv(socket,&c,1,0);
        if(n<=0) return 0;

        if(c==':'){
            len_str[i]='\0';
            break;
        }
        len_str[i++]=c;
        if(i>=19)return 0;
    }

    int length=atoi(len_str);
    if(length>=BUFFER_SIZE) length=BUFFER_SIZE-1;

    int total_read=0;
    while(total_read<length){
        n=recv(socket, buffer+total_read, length-total_read,0);
        if(n<=0) return 0;
        total_read+=n;
    }

    buffer[length]='\0';
    return 1;
}

void remove_users_from_file(int file_index, int socket_client){
    if(file_index==-1)return;
    int locald_idx=-1;

    for(int k=0; k<2; k++)
        {if(files[file_index].editor_sockets[k] == socket_client) locald_idx = k;}

    if(locald_idx!=-1){
        files[file_index].editor_sockets[locald_idx]=0;
        files[file_index].editor_cursors[locald_idx]=-1;
        files[file_index].editor_users[locald_idx][0]='\0';
        if(files[file_index].active_users>0)
            files[file_index].active_users--;
        send_update_to_file(&files[file_index]);
        send_fileinfo_to_file(&files[file_index]);

        printf("[Server] Un utilizator a iesit de pe fisierul %s",files[file_index].filename);
        
    }
}

void respond(struct thData *tdL){
    char buffer[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    char *option;
    char *arg1, *arg2,*arg3;

    int is_logged=0;
    char current_user[100]="";
    int pos_current_file=-1; // nu editam nimic

    while(receiveMess(tdL->cl, buffer)){

        char buffer_copy[BUFFER_SIZE];
        strcpy(buffer_copy,buffer);

        option=strtok(buffer_copy," ");
        arg1=strtok(NULL," ");
        arg2=strtok(NULL," ");
        arg3=strtok(NULL," ");

        printf("[Thread %d] Comanda primita: '%s'\n", tdL->id_thread, buffer);
        memset(response,0,BUFFER_SIZE);

        if(option==NULL){
            strcpy(response,"Eroare: nicio comanda selectata!");
        }else if(strcmp(option,"help")==0){
            strcpy(response,"==== MENIU COMENZI ====\n"
                "1. login <username> <password>\n"
                "2. register <username> <password> <confirm_pasword>\n"
                "3. create <file_name>\n"
                "4. download <file_name> *<path_download_location>\n"
                "5. files\n"
                "6. remove <file_name>\n"
                "7. edit <file_name>\n"
                " --- MENIU EDIT MODE ---\n"
                "   insert <position> <text>\n"
                "   delete <position> <length>\n"
                "   save\n"
                "   exit_edit\n"
                "------------------------\n"
                "8. logout\n"
                "9. quit\n"
                "10. help\n"
                "=======================\n"
            );

        }
        //login <username> <password>
        else if(strcmp(option,"login")==0){
            if(arg1==NULL || arg2==NULL){
                strcpy(response,"Eroare: Lipseste username sau parola. Incearca login <username> <password>");
            }else{
                int loginStatus=checkLoginBD(arg1,arg2);
                if(loginStatus==0){
                is_logged=1;
                strcpy(current_user,arg1);
                sprintf(response,"%s s-a logat cu succes ",current_user);
                printf("[Server] Utilizatorul %s s-a logat cu succes\n",current_user);
                }else if(loginStatus==1){
                    strcpy(response,"Eroare: utilizatorul nu exista. Inregistreaza-te intai");
                }else if(loginStatus==2){
                    strcpy(response,"Eroare: Parola incorecta");
                }else{
                    strcpy(response,"Eroare la baza de date");
                }
                
            }
        }// end login
        //register <username> <password> <confirm_password>
        else if(strcmp(option,"register")==0){
            if(is_logged){
                strcpy(response,"Eroare: Esti deja logat! Pentru a inregistra un nou utilizator mai intai deconecteaza-te de la contul curent");
            }
            else if(arg1==NULL || arg2==NULL || arg3==NULL){
                strcpy(response,"Eroare: Lipseste username sau parola sau confirmarea parolei. Incearca register <username> <password> <confirm_password>");
            }else if(strcmp(arg2,arg3)!=0){
                strcpy(response,"Eroare: parola si confirmarea acesteia nu coincid!");
            }
            else if(!validareParola(arg2)){
                strcpy(response,"Eroare: parola slaba! Necesita minimum 8 caractere, 1 majuscula, 1 minuscula, 1 cifra");
            }else{
                int result=registerUserBD(arg1,arg2);

                if(result==0)
                {   is_logged=1;
                    strcpy(current_user,arg1);
                    sprintf(response,"Utilizatorul a fost inregistrat cu succes! Esti acum logat ca %s",arg1);
                } else if(result==1){
                    strcpy(response,"Eroare: Acest username este deja luat");
                }else{
                    strcpy(response,"Eroare: Problema la baza de date");
                }
            }
        }//end register
        //create
        else if(strcmp(option,"create")==0){
            if(!is_logged){
                strcpy(response,"Eroare: Intai trebuie sa te loghezi!");
            }
            else if(arg1==NULL){
                strcpy(response,"Eroare: Lipseste numele fisierului! Incearca create <nume_fisier>");
            }
            else {
                int place=-1;
                int exists=0;
                pthread_mutex_lock(&files_mutex);
                for(int i=0;i<MAX_FILES;i++){
                    if(files[i].exists && strcmp(files[i].filename, arg1)==0){
                        exists=1;
                        break;
                    }
                    if(!files[i].exists && place==-1) place=i;
                }
                if(exists){
                    pthread_mutex_unlock(&files_mutex);
                    strcpy(response,"Eroare: Fisier deja existent!\n");
                }
                else if(place!=-1){
                    pthread_mutex_lock(&files[place].lock);
                    files[place].exists=1;
                    files[place].active_users=0;
                    files[place].editor_sockets[0]=0; 
                    files[place].editor_sockets[1]=0;
                    files[place].editor_cursors[0]=-1; 
                    files[place].editor_cursors[1]=-1;
                    files[place].editor_users[0][0]='\0';
                    files[place].editor_users[1][0]='\0';
                    strcpy(files[place].filename,arg1);
                    strcpy(files[place].continut,"");
                    pthread_mutex_unlock(&files[place].lock);
                    pthread_mutex_unlock(&files_mutex);

                    saveFileDB(arg1,"");
                    printf("[Server] %s a creat fisierul %s\n",current_user,arg1);
                    sprintf(response,"Fisierul %s a fost creat!",arg1);    
                }else{
                    pthread_mutex_unlock(&files_mutex);
                    strcpy(response, "Nu mai este spatiu de stocare pe server!\n");
                }
            }
            //end create
        }//files list
        else if(strcmp(option,"files")==0){
            if(!is_logged){
                strcpy(response,"Eroare: E nevoie sa va logati intai!");
            }else{
                char list_of_files[4096];
                struct FileListBuffer list={list_of_files, sizeof(list_of_files)};
                snprintf(list_of_files,sizeof(list_of_files),"====Fisiere in Server====\n");

                pthread_mutex_lock(&db_mutex);
                sqlite3_exec(database,"SELECT FILENAME FROM FILES;",files_list,&list,0);
                pthread_mutex_unlock(&db_mutex);

                sendMess(tdL->cl,list_of_files);
                strcpy(response," ");

            }
        }//end files list
        // download
        else if(strcmp(option,"download")==0){
            if(!is_logged)
            {
                strcpy(response,"Eroare: E nevoie sa va logati intai!");
            }
            else if(pos_current_file!=-1)
            {
                strcpy(response,"Eroare: Trebuie sa iesi din edit mode (exit_edit) ca sa descarci orice fisier!");      
            }
            else if(arg1==NULL)
            {
                strcpy(response,"Eroare: lipseste numele fisierului!");

            }
            else{
                int found=-1;
                pthread_mutex_lock(&files_mutex);
                for(int i=0; i<MAX_FILES;i++){
                    if(files[i].exists && strcmp(files[i].filename,arg1)==0){
                        found=i;
                        break;
                    }
                }
                if(found!=-1){
                    pthread_mutex_lock(&files[found].lock);
                }
                pthread_mutex_unlock(&files_mutex);

                if(found !=-1 && files[found].exists){
                    int necesita_size=strlen(files[found].continut)+20;
                    char *buffer_continut=(char*)malloc(necesita_size);

                    if(buffer_continut){
                        sprintf(buffer_continut,"CONTENT:%s",files[found].continut);
                        pthread_mutex_unlock(&files[found].lock);
                        sendMess(tdL->cl,buffer_continut);
                        printf("[Server] Fisierul %s a fost descarcat! de %s",arg1,current_user);
                        free(buffer_continut);
                    }else{
                        pthread_mutex_unlock(&files[found].lock);
                        sendMess(tdL->cl,"Eroare: Memorie insuficienta pe server");
                    }
                    strcpy(response,"");
                }else{
                    if(found!=-1){
                        pthread_mutex_unlock(&files[found].lock);
                    }
                    strcpy(response,"Eroare: Fisierul nu exista pe server");
            }
            }
            //end download
        }
        //remove
        else if(strcmp(option,"remove")==0){
            if(!is_logged){
                strcpy(response,"Eroare: Intai trebuie sa te loghezi!");
            }
            else if(arg1==NULL){
                strcpy(response,"Eroare: lipseste numele fisierului!");
            }else{
                int found=-1;
                pthread_mutex_lock(&files_mutex);
                for(int i=0;i<MAX_FILES;i++){
                    if(files[i].exists && strcmp(files[i].filename,arg1)==0){
                        found=i;
                        break;

                    }
                }
                if(found!=-1){
                    pthread_mutex_lock(&files[found].lock);
                }
                pthread_mutex_unlock(&files_mutex);

                if(found!=-1 && files[found].exists){
                    if(files[found].active_users>0){
                        sprintf(response,"Eroare: Fisierul este deschis de %d utilizatori ;i nu poate fi sters acum",files[found].active_users);
                        pthread_mutex_unlock(&files[found].lock);
                    }
                    else{
                        //stergere din ram
                        files[found].exists=0;
                        files[found].active_users=0;
                        files[found].editor_sockets[0]=0;
                        files[found].editor_sockets[1]=0;
                        files[found].editor_cursors[0]=-1;
                        files[found].editor_cursors[1]=-1;
                        files[found].editor_users[0][0]='\0';
                        files[found].editor_users[1][0]='\0';
                        files[found].filename[0]='\0';
                        strcpy(files[found].continut,"");
                        pthread_mutex_unlock(&files[found].lock);
                        deleteFileDB(arg1);
                        sprintf(response,"Fisierul %s a fost sters definitiv",arg1);
                    }
                }else{
                    strcpy(response,"Eroare: Fisierul nu exista");
                    if(found!=-1){
                        pthread_mutex_unlock(&files[found].lock);
                }
                }
        }
            //end remove
        }
        //edit mode, maxim pentru 2 clienti pe acelasi document la un moment dat
        else if(strcmp(option,"edit")==0){
            if(!is_logged) strcpy(response,"Eroare: Intai trebuie sa te loghezi!");
            else if(arg1==NULL) strcpy(response,"Eroare: lipseste numele fisierului! Incearca edit <file_name>");
            else{
            int gasit=-1;
            pthread_mutex_lock(&files_mutex);
            for(int i=0;i<MAX_FILES;i++){
                if(files[i].exists && strcmp(files[i].filename, arg1)==0){
                    gasit=i;
                    break;
                }
                }
                if(gasit!=-1){
                    pthread_mutex_lock(&files[gasit].lock);
                }
                pthread_mutex_unlock(&files_mutex);

                if(gasit==-1){
                    strcpy(response,"Eroare: Fisierul pe care vrei sa-l editezi nu a fost gasit!");
                }
                else{
                    if(!files[gasit].exists){
                        strcpy(response,"Eroare: Fisierul pe care vrei sa-l editezi nu a fost gasit!");
                        pthread_mutex_unlock(&files[gasit].lock);
                    }
                    else if(files[gasit].active_users<2){
                    files[gasit].active_users++;
                    pos_current_file=gasit;

                    for(int k=0;k<2;k++)
                    if(files[gasit].editor_sockets[k]==0){
                    files[gasit].editor_sockets[k]=tdL->cl;
                    files[gasit].editor_cursors[k]=0;
                    strncpy(files[gasit].editor_users[k], current_user, sizeof(files[gasit].editor_users[k]) - 1);
                    files[gasit].editor_users[k][sizeof(files[gasit].editor_users[k]) - 1]='\0';
                    break;
                    }
                    send_update_to_file(&files[gasit]);
                    send_fileinfo_to_file(&files[gasit]);
                    strcpy(response,"");
                    pthread_mutex_unlock(&files[gasit].lock);
                    }
                    else{
                        strcpy(response,"Eroare: Acest document nu accepta editori in plus momentan!");
                        pthread_mutex_unlock(&files[gasit].lock);
                    }
                }
            }
            //end edit

        }// save changes made in edit mode
        else if(strcmp(option,"save")==0){
            if(pos_current_file==-1)
            strcpy(response,"Eroare: trebuie sa fii in modul de editare al fisierului!");
            else{
                pthread_mutex_lock(&files[pos_current_file].lock);
                if(!files[pos_current_file].exists){
                    strcpy(response,"Eroare: Fisierul nu mai exista");
                }else{
                    saveFileDB(files[pos_current_file].filename,files[pos_current_file].continut);
                    strcpy(response,"Salvat in BD");
                    sprintf(response,"Fisierul '%s' a fost salvat cu succes in baza de date!",files[pos_current_file].filename);
                }
                pthread_mutex_unlock(&files[pos_current_file].lock);
            }
        // end save
        }
        //insert <position> <char>
        else if(strcmp(option,"insert")==0){
            if(pos_current_file==-1)
            {strcpy(response,"Eroare: trebuie sa fii in modul de editare al fisierului!");}
            // strcpy(response,"Eroare: Incearca insert <position> <char>");
            else if(pos_current_file!=-1 && arg1){
                pthread_mutex_lock(&files[pos_current_file].lock);
                if(!files[pos_current_file].exists){
                    strcpy(response,"Eroare: Fisierul nu mai exista");
                    pthread_mutex_unlock(&files[pos_current_file].lock);
                }else{
                char *text=buffer;
                while(*text &&  !isspace(*text))text++;
                while(*text && isspace(*text)) text++;

                while(*text && !isspace(*text))text++;
                if(*text && isspace(*text)) text++;

                if(text && strlen(text)>0){
                int pos=atoi(arg1);

                char *src = text;
                        char *dst = text;
                        while (*src)
                        {
                            if(*src == '\\' && *(src+1)=='n')
                            {
                                *dst++ = '\n'; 
                                src += 2;      
                            }else{
                                *dst++ = *src++; 
                            }
                        }
                        *dst = '\0';

                int text_len=strlen(text);
                int editor_idx=editor_index_socket(&files[pos_current_file], tdL->cl);

                inserting(&files[pos_current_file],pos,text);
                if(editor_idx!=-1){
                    files[pos_current_file].editor_cursors[editor_idx] = pos + text_len;
                    for(int k=0;k<2;k++){
                        if(k==editor_idx)
                         continue;
                        if(files[pos_current_file].editor_sockets[k]==0) continue;
                        if(files[pos_current_file].editor_cursors[k] >= pos){
                            files[pos_current_file].editor_cursors[k] += text_len;
                        }
                    }
                }
                send_update_to_file(&files[pos_current_file]);
                saveFileDB(files[pos_current_file].filename, files[pos_current_file].continut);

                strcpy(response,"S-a inserat in fisier!");
                }
                pthread_mutex_unlock(&files[pos_current_file].lock);
              //  sprintf(response,"Se va insera textul dat in fisier!");
            
                } 
                }
        }
        //delete <position> <length> (positive length for delete, negative for backspace)
        else if(strcmp(option,"delete")==0){
            if(pos_current_file==-1)
            strcpy(response,"Eroare: Trebuie sa fii in modul de editare al fisierului!");
            else if(!arg1)
            {
                strcpy(response,"Eroare: Incearca delete <position> <length>");
            }else{
                int pos=atoi(arg1);
                int len=(arg2!=NULL) ? atoi(arg2) : 1;

                pthread_mutex_lock(&files[pos_current_file].lock);
                if(!files[pos_current_file].exists){
                    strcpy(response,"Eroare: Fisierul nu mai exista");
                }
                else
                {   int del_start=0;
                    int del_len=0;
                    int editor_idx=editor_index_socket(&files[pos_current_file], tdL->cl);

                    delete_helper(files[pos_current_file].continut, pos, len, &del_start, &del_len);
                    if(del_len>0){
                        deleting(&files[pos_current_file],del_start,del_len);

                        if(editor_idx!=-1)
                        {
                        files[pos_current_file].editor_cursors[editor_idx]=del_start;

                            for(int k=0;k<2;k++)
                            {
                            if(k==editor_idx) continue;
                            if(files[pos_current_file].editor_sockets[k]==0) continue;
                            if(files[pos_current_file].editor_cursors[k]>del_start){
                                if(files[pos_current_file].editor_cursors[k]>=del_start+del_len){
                                    files[pos_current_file].editor_cursors[k]-=del_len;
                                }else{
                                    files[pos_current_file].editor_cursors[k] = del_start;
                                }
                            }
                            }
                        }
                        send_update_to_file(&files[pos_current_file]);
                        saveFileDB(files[pos_current_file].filename,files[pos_current_file].continut);
                    }
                    strcpy(response,"");
                }
                pthread_mutex_unlock(&files[pos_current_file].lock);
            }
            
//end delete
        }//exit_edit
        else if(strcmp(option,"exit_edit")==0){
            if(pos_current_file==-1){
                strcpy(response,"Eroare: Nu esti in edit mode!");
            }
            else{
                pthread_mutex_lock(&files[pos_current_file].lock);

                sprintf(response,"S-a inchis editarea fisierului %s\n",files[pos_current_file].filename);
                remove_users_from_file(pos_current_file,tdL->cl);
                pthread_mutex_unlock(&files[pos_current_file].lock);

                pos_current_file=-1;
            }
            //end exit_edit

        }
    //logout / quit
        else if(strcmp(option,"logout")==0 || strcmp(option,"quit")==0){
            is_logged=0; 
            sprintf(response,"User %s s-a delogat!\n",current_user);
           
            if(pos_current_file!=-1)
            {            
            pthread_mutex_lock(&files[pos_current_file].lock);
            remove_users_from_file(pos_current_file,tdL->cl);
            pthread_mutex_unlock(&files[pos_current_file].lock);
            pos_current_file=-1;
        }
        strcpy(current_user,"");
    
    } // end logout
        else{
            strcpy(response,"Comanda nerecunoscuta!\n");
        }
        sendMess(tdL->cl, response);

        }
//deconectare fortata
    if(pos_current_file!=-1){
        pthread_mutex_lock(&files[pos_current_file].lock);
        remove_users_from_file(pos_current_file,tdL->cl);

        printf("[Thread %d] Client deconectat fortat. S-a eliberat fisierul %s.\n",tdL->id_thread,files[pos_current_file].filename);
        pthread_mutex_unlock(&files[pos_current_file].lock);
    }
    printf("[Thread %d] Clientul a inchis conexiunea.\n",tdL->id_thread);
}

int main()
{
    struct sockaddr_in server;
    struct sockaddr_in receive;
    int sd;
    int pid;
    pthread_t th[100];
    int i=0;
    initializareDB(); // deschide bd si creaza tabele
    loadFromDB(); //populeaza vcetorul din ram cu ce e in tabelele

    //socket create

    if(-1==(sd=socket(AF_INET,SOCK_STREAM,0)))
    {
        perror("[Server] Eroare la creare socket!\n");
        return errno;
    }

    int on=1;
    setsockopt(sd,SOL_SOCKET, SO_REUSEADDR,&on,sizeof(on));

    bzero(&server, sizeof(server));
    bzero(&receive,sizeof(receive));

    server.sin_family=AF_INET;
    server.sin_addr.s_addr=htonl(INADDR_ANY);
    server.sin_port=htons(PORT);

    //bind
    if(-1==bind(sd, (struct sockaddr *) &server, sizeof(struct sockaddr)))
    {
        perror("[Server] Eroare la bind!");
        return errno;
    }

    //listen

    if(listen(sd,5)==-1)
    {
        perror("[Server] Eroare la listen!");
        return errno;
    }
    printf("[Server] CollaborativeNotepad pornit pe portul %d...\n",PORT);

    //running server with threads
    while(1){
        int client;
        thData * td;
        socklen_t len=sizeof(receive);
        printf("[Server] CollaborativeNotepad asteapta la portul %d...\n",PORT);
        fflush(stdout);

        if((client=accept(sd,(struct sockaddr *) &receive, &len))<0)
        {
            perror("[Server] Eroare la accept!");
            continue;
        }        
        
        td=(struct thData*)malloc(sizeof(struct thData));
        td->id_thread=i++;
        td->cl=client;
        pthread_t th;
        pthread_create(&th,NULL,&treat,td);
    }

    sqlite3_close(database);
    return 0;
};

static void *treat(void * arg){
    struct thData *tdL;
    tdL= (struct thData*)arg;
    printf("[Thread %d] In asteptarea unui mesaj...\n",tdL->id_thread);
    fflush(stdout);
    pthread_detach(pthread_self());

    respond((struct thData*)arg);

    //close client connection
    close (tdL->cl);
    free(tdL);
    printf("[Thread] Session closed\n");
    return(NULL);
}
