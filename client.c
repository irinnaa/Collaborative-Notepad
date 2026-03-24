//CollaborativeNotepad client
//Compilare: gcc client.c -o client -lncurses
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <ncurses.h>
#include <stdarg.h>
#include <signal.h>
#include <ctype.h>
#include <sys/stat.h>

#define BUFFER_SIZE 1024

extern int errno;
int port;

WINDOW *win_document;
WINDOW *win_cmd;
int max_y,max_x;

char *continut_doc_copy=NULL;
int scroll_offset=0;

int local_cursor=0;
int remote_cursor=-1;
static int awaiting_ack=0;

char current_filename[50]="";
char current_users[128]="";

void itoa(int n,char s[],int base){
    int i=0;
    int sign=n;
    if(n<0) n=-n;

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

void sendMess(int socket,const char *message){
    if(message==NULL)return;
    int bytes=strlen(message);

    char bytes_formatted[50];
    itoa(bytes,bytes_formatted,10);

    int header_len=strlen(bytes_formatted)+1;
    int total_allocated=header_len+bytes+1;

    char *packet=(char*)malloc(total_allocated*sizeof(char));
    if(packet==NULL){
        perror("[Client] Eroare la alocarea memorie in sendMess\n");
        return;
    }

    packet[0]='\0';
    
    strcat(packet,bytes_formatted);
    strcat(packet,":");
    strcat(packet,message);

    if(send(socket,packet,strlen(packet),0)<0){
        perror("[Client] Eroare la trimiterea mesajului!\n");
    }
    free(packet);
}

static int file_color_pair(const char *filename){
    static const short pairs[] = {5, 6, 7, 8, 9, 10};
    if(filename==NULL || filename[0]=='\0') return 1;
    unsigned int hash=0;
    for(const unsigned char *p=(const unsigned char*)filename; *p; p++){
        hash = (hash * 31u) + *p;
    }
    return pairs[hash % (sizeof(pairs) / sizeof(pairs[0]))];
}

static void clear_file_info(){
    current_filename[0]='\0';
    current_users[0]='\0';
}

char* receiveMess(int socket){
    char len_str[20];
    int i=0;
    char c;
    int n;

    while(1){
        n=recv(socket,&c,1,0);
        if(n<=0)return NULL;

        if(c==':'){
            len_str[i]='\0';
            break;
        }
        len_str[i++]=c;
        if(i>=19)return NULL;
    }

    int length=atoi(len_str);
    char *buffer=(char*)malloc((length+1)*sizeof(char));
    if(buffer==NULL){
        perror("[Client] Memorie insufiecienta pentru mesaj");
        return NULL;
    }

    int total_read=0;
    while(total_read<length){
        n=recv(socket, buffer+total_read, length-total_read,0);
        if(n<=0) 
        {
            free(buffer);
            return NULL;
        }
        total_read+=n;
    }

    buffer[length]='\0';
    return buffer;
}

int is_directory(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0) return 0; 
    return S_ISDIR(statbuf.st_mode); 
}

void path_extins(const char *input, char *output) {
    if (input[0] == '~') {
        const char *home = getenv("HOME");
        if (home) {
            sprintf(output, "%s%s", home, input + 1);
        } else {
            strcpy(output, input);
        }
    } else {
        strcpy(output, input);
    }
}


// functii grafica ncurses

void render_doc_view(){
    wclear(win_document);
    int bg_pair=(current_filename[0]=='\0') ? 1 : file_color_pair(current_filename);
    wbkgd(win_document,COLOR_PAIR(bg_pair));
    box(win_document,0,0);
    char header[256];
    if(current_filename[0]=='\0'){
        snprintf(header,sizeof(header),"MAIN");
    }else if(current_users[0]!='\0'){
        snprintf(header,sizeof(header),"File: %s | Users: %s | Line: %d | Cursor: %d",current_filename,current_users,scroll_offset,local_cursor);
    }else{
        snprintf(header,sizeof(header),"File: %s | Users: - | Line: %d | Cursor: %d",current_filename,scroll_offset,local_cursor);
    }
    int max_title=getmaxx(win_document)-4;
    if(max_title < 0) max_title = 0;
    mvwprintw(win_document,0,2,"%.*s",max_title,header);

    if(continut_doc_copy!=NULL){
        int row=1;
        int col=1;
        int global_index=0;
        int current_line=0;
        int max_rows=getmaxy(win_document)-2;
        int max_cols=getmaxx(win_document)-2;
        char *p=continut_doc_copy;

        while(*p && current_line<scroll_offset){
            if(*p=='\n')
            current_line++;
            p++;
            global_index++;
        }

        int printed_rows=0;
        while(*p && printed_rows<max_rows){
            int e_local=(global_index==local_cursor);
            int e_remote=(remote_cursor!=-1 && global_index==remote_cursor);

            if(e_local) wattron(win_document,COLOR_PAIR(4) | A_BOLD | A_BLINK);
            else if(e_remote) wattron(win_document,COLOR_PAIR(3) | A_BOLD);

            if(*p=='\n'){
                if(e_local || e_remote) waddch(win_document,' ');

                if(e_local) wattroff(win_document, COLOR_PAIR(4) |A_BOLD | A_BLINK);
                else if(e_remote) wattroff(win_document, COLOR_PAIR(3) | A_BOLD);

                
                row++;
                col=1;
                printed_rows++;
            }
            else{
                wmove(win_document,row,col);
                waddch(win_document,*p);

                if(e_local) wattroff(win_document,COLOR_PAIR(4) | A_BOLD | A_BLINK);
                else if(e_remote) wattroff(win_document,COLOR_PAIR(3) | A_BOLD);

                col++; //warp la text
                if(col>max_cols){
                    row++;
                    col=1;
                    printed_rows++;
                }
            }
            p++;
            global_index++;
        }

        int e_local=(global_index==local_cursor);
        int e_remote=(remote_cursor!=-1 && global_index==remote_cursor);

        if(e_local || e_remote){
            wmove(win_document,row,col);
            if(e_local)
            wattron(win_document,COLOR_PAIR(4) |A_BOLD|A_BLINK);
            else  wattron(win_document,COLOR_PAIR(3) |A_BOLD);

            waddch(win_document,' ');
            if(e_local)
            wattroff(win_document,COLOR_PAIR(4) | A_BOLD | A_BLINK);
            else
            wattroff(win_document,COLOR_PAIR(3) | A_BOLD );
        }
        

    }else{
        mvwprintw(win_document,1,1,"Niciun fisier deschis. Foloseste 'open <filename>' sau 'create'");

    }
    wrefresh(win_document);
}

void init_ncurses(){

    initscr(); 
    cbreak();  
    noecho();  
    keypad(stdscr,TRUE); 
    start_color();

    init_pair(1,COLOR_WHITE,COLOR_BLUE);
    init_pair(2, COLOR_GREEN, COLOR_BLACK);
    init_pair(3,COLOR_RED, COLOR_BLACK);
    init_pair(4,COLOR_BLACK, COLOR_CYAN);
    init_pair(5,COLOR_WHITE, COLOR_GREEN);
    init_pair(6,COLOR_WHITE, COLOR_RED);
    init_pair(7,COLOR_BLACK, COLOR_YELLOW);
    init_pair(8,COLOR_WHITE, COLOR_MAGENTA);
    init_pair(9,COLOR_BLACK, COLOR_WHITE);
    init_pair(10,COLOR_WHITE, COLOR_BLACK);


    getmaxyx(stdscr,max_y,max_x);

    win_document=newwin(max_y-5,max_x,0,0);
    win_cmd=newwin(5,max_x,max_y-5,0);

    wbkgd(win_document,COLOR_PAIR(1));
    render_doc_view();
    box(win_cmd,0,0);
   // mvwprintw(win_document,0,2,"CollaborativeNotepad in asteptare...");
   mvwprintw(win_cmd,3,2,"COMMAND > ");

    wrefresh(win_cmd);
}

void close_ncurses(){
    if(continut_doc_copy)
    free(continut_doc_copy);
    delwin(win_document);
    delwin(win_cmd);
    endwin();
}

void read_input_with_scroll(char *buffer, int max_len){
    int pos=0;
    int ch;
    memset(buffer,0,max_len);
    keypad(win_cmd,TRUE);

    while(1){
        ch=wgetch(win_cmd);

        if(ch=='\n' || ch==10 || ch==13)break;
        else if(ch==KEY_BACKSPACE || ch==127 || ch=='\b'){
            if(pos>0){
                pos--;
                buffer[pos]='\0';
                mvwaddch(win_cmd,3,12+pos,' ');
                wmove(win_cmd,3,12+pos);
                wrefresh(win_cmd);

            }
        }
        //scroll
        else if(ch==KEY_PPAGE) //pageup
        {
            if(scroll_offset>5)scroll_offset-=5;
            else scroll_offset=0;
            render_doc_view();
            wrefresh(win_cmd);
        }
        else if(ch==KEY_NPAGE){ //pagedown
            scroll_offset+=5;
            render_doc_view();
            wrefresh(win_cmd);
        }
        else if(ch==KEY_UP) //keyup
        {
            if(scroll_offset>0)scroll_offset--;
            render_doc_view();
            wrefresh(win_cmd);
        }
        else if(ch==KEY_DOWN){ //keydown
            scroll_offset++;
            render_doc_view();
            wrefresh(win_cmd);
        }
        else if(ch==KEY_RIGHT){
            if(continut_doc_copy && local_cursor <strlen(continut_doc_copy))
            local_cursor++;
            render_doc_view();
            wrefresh(win_cmd);
        }
        else if(ch==KEY_LEFT){
            if(local_cursor>0)
            local_cursor--;
            render_doc_view();
            wrefresh(win_cmd);
        }
    
    else if(ch>=32 && ch<=126){ //char normale
        if(pos<max_len-1){
            buffer[pos++]=ch;
            waddch(win_cmd,ch);
            wrefresh(win_cmd);
        }
    }
}
}

static void drain_pending_input(void){
    int ch;
    nodelay(win_cmd, TRUE);
    while((ch = wgetch(win_cmd)) != ERR){
        (void)ch;
    }
    nodelay(win_cmd, FALSE);
}

void update_doc_content(const char *new_content){
    if(continut_doc_copy !=NULL)
    free(continut_doc_copy);

    char *first_colon=strchr(new_content,':');
    char *text_start=(char*)new_content;
    int c1=0;
    int c2=0;
    int cursoare=0;

    if(first_colon){
        char *second_colon=strchr(first_colon+1,':');
        if(second_colon){
            cursoare=1;
            
            c1=atoi(new_content);
            c2=atoi(first_colon+1);
            text_start=second_colon+1;
        }
    }

    if(cursoare){
        if(c1 < 0) c1 = 0;
        local_cursor = c1;
        remote_cursor = (c2 >= 0) ? c2 : -1;
    }else{
        remote_cursor = -1;
    }
    continut_doc_copy=strdup(text_start);

    if(continut_doc_copy){
        int content_len=strlen(continut_doc_copy);
        if(local_cursor>content_len){
            local_cursor=content_len;
        }
        if(remote_cursor>content_len){
            remote_cursor=content_len;
        }
    }

    render_doc_view();
}

void update_file_info(const char *info){
    const char *colon=strchr(info,':');
    if(!colon) return;

    size_t name_len=(size_t)(colon - info);
    if(name_len >= sizeof(current_filename)) name_len = sizeof(current_filename) - 1;
    memcpy(current_filename, info, name_len);
    current_filename[name_len]='\0';

    const char *users=colon + 1;
    strncpy(current_users, users, sizeof(current_users) - 1);
    current_users[sizeof(current_users) - 1]='\0';

    render_doc_view();
}

void log_to_cmd(const char *message){
    wclear(win_cmd);
    box(win_cmd,0,0);
    mvwprintw(win_cmd,1,2,"SERVER: %s",message);
    mvwprintw(win_cmd,3,2,"COMMAND > ");
    wrefresh(win_cmd);
}

void wlog(const char *format,...){
    char buffer[BUFFER_SIZE];
    va_list args;
    va_start(args,format);
    vsnprintf(buffer,BUFFER_SIZE,format,args);
    va_end(args);

    wclear(win_cmd);
    
    box(win_cmd,0,0);

    int max_width=getmaxx(win_cmd)-4;
    mvwprintw(win_cmd,1,2,"MSG: %.*s",max_width,buffer);
    mvwprintw(win_cmd,3,2,"COMMAND > ");
    wrefresh(win_cmd);
}
void help_popup(){
    int h=18, w=60;
    int y=(max_y-h)/2;
    int x=(max_x-w)/2;

    WINDOW *popup=newwin(h,w,y,x);
    wbkgd(popup,COLOR_PAIR(1) | A_BOLD );
    box(popup,0,0);

    mvwprintw(popup,0,2,"=== HELP - Apasa pe ESC pentru a inchide ===");
    mvwprintw(popup,1,2,"1. login <username> <password>");
    mvwprintw(popup,2,2,"2. register <username> <password> <confirm_pasword>");
    mvwprintw(popup,3,2,"3. create <file_name>");
    mvwprintw(popup,4,2,"4. open <file_name> (sau edit <file_name>)");
    mvwprintw(popup,5,2,"5. download <file_name> *<path_download_location>");
    mvwprintw(popup,6,2,"6. files");
    mvwprintw(popup,7,2,"7. remove <file_name>");
    mvwprintw(popup,8,2,"--- MENIU EDIT MODE ---");
    mvwprintw(popup,9,2,"  insert <position> <text>");
    mvwprintw(popup,10,2,"  delete <position> <length>");
    mvwprintw(popup,11,2,"  save");
    mvwprintw(popup,12,2,"  exit_edit");
    mvwprintw(popup,13,2,"------------------------");
    mvwprintw(popup,14,2,"8. logout");
    mvwprintw(popup,15,2,"9. quit");
    mvwprintw(popup,16,2,"10. help");
    mvwprintw(popup,17,2,"=====================================");

    wrefresh(popup);

    while(1){
        int ch=wgetch(popup);
        if(ch==27) //esc
        break;
    }

    delwin(popup);

    touchwin(win_document);
    touchwin(win_cmd);
    render_doc_view();
    wrefresh(win_cmd);
}


void files_popup(char *file_list){
    int h=20;
    int w=60;
    int y=(max_y - h) / 2;
    int x = (max_x - w) / 2;

    WINDOW *popup = newwin(h, w, y, x);
    wbkgd(popup, COLOR_PAIR(1) | A_BOLD);
    box(popup, 0, 0);

    mvwprintw(popup,0,2,"=== FILES - Apasa pe ESC pentru a inchide ===");

    int row=2;
    char *line=strtok(file_list,"\n");
    while(line!=NULL && row <h-1){
        mvwprintw(popup, row, 2, "%s", line);
        line = strtok(NULL, "\n");
        row++;
    }

    wrefresh(popup);
    while(wgetch(popup)!=27); //asteapta esc

    delwin(popup);
    touchwin(win_document);
    touchwin(win_cmd);
    render_doc_view();
    wrefresh(win_cmd);

}

void update_doc_view(const char *continut){
    wclear(win_document);

    wbkgd(win_document,COLOR_PAIR(1));
    clear_file_info();
    remote_cursor=-1;
    local_cursor=0;
    scroll_offset=0;

    mvwprintw(win_document,1,1,"%s",continut);
    box(win_document,0,0);
    mvwprintw(win_document,0,2,"MAIN");
    wrefresh(win_document);
}

void handle_winch(int signal){
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr,max_y,max_x);
    wresize(win_document,max_y-5,max_x);
    wresize(win_cmd,5,max_x);
    mvwin(win_cmd,max_y-5,0);

    render_doc_view();

    box(win_cmd,0,0);
    mvwprintw(win_cmd,3,2,"COMMAND > ");

   // wrefresh(win_document);
    wrefresh(win_cmd);
}

//end functii ncurses

int main(int argc, char*argv[]){
    int sd;
    struct sockaddr_in server;
    char input_buf[BUFFER_SIZE]; //buffer mic pt citit input

    if(argc!=3){
    wlog("Sintaxa: %s <adresa_server> <port>\n",argv[0]);
    return -1;
    }

    port=atoi(argv[2]);

    //socket create
    if(-1==(sd=socket(AF_INET,SOCK_STREAM,0)))
    {
        log_to_cmd("[Client] Eroare la socket!\n");
        return errno;
    }

    memset(&server,0,sizeof(server));
    server.sin_family=AF_INET;
    server.sin_addr.s_addr=inet_addr(argv[1]);
    server.sin_port=htons(port);

    char download_path_pending[2048] = ""; 
    int waiting_for_download = 0;

    //server connection
    if(-1==connect(sd,(struct sockaddr *) &server, sizeof (struct sockaddr)))
    {
        wlog("[Client] Eroare la connect!\n");
        return errno;
    }
    init_ncurses();
    signal(SIGWINCH,handle_winch);

    char *start_msg="==== MENIU COMENZI ====\n"
                " 1. login <username> <password>\n"
                " 2. register <username> <password> <confirm_pasword>\n"
                " 3. create <file_name>\n"
                " 4. edit <file_name> (sau open <file_name>\n"
                " 5. download <file_name> *<path_download_location>\n"
                " 6. files\n"
                " 7. remove <file_name>\n"
                "  --- MENIU EDIT MODE ---\n"
                "   insert <position> <text>\n"
                "   delete <position> <length>\n"
                "   save\n"
                "   exit_edit\n"
                "------------------------\n"
                " 8. logout\n"
                " 9. quit\n"
                " 10. help\n"
                "=======================\n";
    update_doc_view(start_msg);


    wlog("[Client] Conectat la CollaborativeNotepad!\n");
   
   while(1){
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(0,&read_fds);
    FD_SET(sd,&read_fds);

    if(-1==select(sd+1,&read_fds,NULL,NULL,NULL)){
        if(errno==EINTR)continue;
        perror("[Client] Eroare la select");
        break;
    }

    if(FD_ISSET(sd,&read_fds)){
        char *response=receiveMess(sd);
        if(response==NULL){
            delwin(win_document);
            delwin(win_cmd);
            endwin();
            printf("[Client] Serverul s-a deconectat\n");
            break;
        }
        //update live la fisier
        int is_update = (strncmp(response,"UPDATE:",7)==0);
        int is_fileinfo = (strncmp(response,"FILEINFO:",9)==0);
        int is_content = (strncmp(response,"CONTENT:",8)==0);

        if(is_update){
            update_doc_content(response+7);
            box(win_cmd,0,0);
            mvwprintw(win_cmd,3,2,"COMMAND > ");
            wrefresh(win_cmd);
 }
        else if(strncmp(response, "=== FILES ===", strlen("=== FILES ===")) == 0 ||
        strncmp(response, "====Fisiere in Server====", strlen("====Fisiere in Server====")) == 0)
    {
            files_popup(response);
        }
        else if(is_fileinfo){
            update_file_info(response+9);
            box(win_cmd,0,0);
            mvwprintw(win_cmd,3,2,"COMMAND > ");
            wrefresh(win_cmd);
        }
else if(is_content){
            
    if(waiting_for_download == 1) {
        const char *content_body = response + 8;
        FILE *fp = fopen(download_path_pending, "w");
        if(fp){
            // Scriem tot ce e dupa "CONTENT:"
            fputs(content_body, fp);
            fclose(fp);
            size_t bytes_recv = strlen(content_body);

            size_t bytes = strlen(content_body);
            if(bytes==0) wlog("ATENTIE: Fisier gol descarcat in %s", download_path_pending);
            else wlog("S-a salvat in %s (%zu bytes)", download_path_pending, bytes);
        } else {
            wlog("Eroare: Nu pot scrie in '%s' (Folder inexistent?)", download_path_pending);
        }
        waiting_for_download = 0;
        download_path_pending[0] = '\0';
    } 
    else {
        wlog("Primit continut fisier neasteptat.");
    }

}
        //mesaj normal primit de la server
        else{
           if(response[0]!='\0'){
               log_to_cmd(response);
           }
        }
        if(awaiting_ack && !is_update && !is_fileinfo){
            awaiting_ack=0;
        }
        free(response);
    }

    if(FD_ISSET(0,&read_fds)){
        if(awaiting_ack){
            drain_pending_input();
            continue;
        }

        mvwprintw(win_cmd,3,2,"COMMAND > ");
        wclrtoeol(win_cmd);
        wrefresh(win_cmd);

        read_input_with_scroll(input_buf,BUFFER_SIZE-1);

        if(strlen(input_buf)==0)continue;

        //comenzi UI

        if(strcmp(input_buf,"exit_edit")==0){
            if(continut_doc_copy){
                free(continut_doc_copy);
                continut_doc_copy=NULL;
            }
            clear_file_info();
            remote_cursor=-1;
            local_cursor=0;
            scroll_offset=0;
            render_doc_view();
        }

        if(strcmp(input_buf,"help")==0){
            help_popup();
            continue;
        }

        if(strcmp(input_buf,"clear")==0){
            if(continut_doc_copy){
                free(continut_doc_copy);
                continut_doc_copy=NULL;
            }
            clear_file_info();
            remote_cursor=-1;
            local_cursor=0;
            scroll_offset=0;
            render_doc_view();
            continue;
        }
        //open alias pt edit

        if(strncmp(input_buf,"open",4)==0){
            input_buf[0]='e';
            input_buf[1]='d';
            input_buf[2]='i';
            input_buf[3]='t';
        }
        if(strcmp(input_buf,"quit")==0 || strcmp(input_buf,"exit")==0){
            wlog("[Client] Se inchide...\n");
            break;
        }

        char cmd_final[BUFFER_SIZE];
        if(strcmp(input_buf,"files")==0){
            strcpy(cmd_final,"files");
        }
        else if(strncmp(input_buf,"insert",6)==0){
            const char *text_after_cmd=input_buf+6;
            if(*text_after_cmd==' ') text_after_cmd++;

            const char *trimmed=text_after_cmd;
            while(*trimmed && isspace((unsigned char)*trimmed)) trimmed++;

            if(*trimmed=='\0'){
                strcpy(cmd_final,input_buf);
            }else{
                char *endptr=NULL;
                long pos=strtol(trimmed,&endptr,10);
                if(endptr!=trimmed){
                    const char *after_num=endptr;
                    while(*after_num && isspace((unsigned char)*after_num)) after_num++;
                    if(*after_num!='\0'){
                        snprintf(cmd_final,sizeof(cmd_final),"insert %ld %s",pos,after_num);
                    }else{
                        snprintf(cmd_final,sizeof(cmd_final),"insert %d %s",local_cursor,text_after_cmd);
                    }
                }else{
                    snprintf(cmd_final,sizeof(cmd_final),"insert %d %s",local_cursor,text_after_cmd);
                }
            }
        }
        else if(strncmp(input_buf,"delete",6)==0){
            const char *p=input_buf+6;
            while(*p && isspace((unsigned char)*p)) p++;

            if(*p=='\0'){
                snprintf(cmd_final,sizeof(cmd_final),"delete %d 1",local_cursor);
            }else{
                char *endptr=NULL;
                long first=strtol(p,&endptr,10);
                if(endptr==p){
                    strcpy(cmd_final,input_buf);
                }else{
                    const char *after_first=endptr;
                    while(*after_first && isspace((unsigned char)*after_first)) after_first++;
                    if(*after_first=='\0'){
                        snprintf(cmd_final,sizeof(cmd_final),"delete %d %ld",local_cursor,first);
                    }else{
                        char *endptr2=NULL;
                        long second=strtol(after_first,&endptr2,10);
                        if(endptr2==after_first){
                            strcpy(cmd_final,input_buf);
                        }else{
                            snprintf(cmd_final,sizeof(cmd_final),"delete %ld %ld",first,second);
                        }
                    }
                }
            }
        }
        else{
            strcpy(cmd_final,input_buf);
        }
        
        //end comezi UI
   
    char path_destinatie[256]=""; 
    char comandasv[BUFFER_SIZE];
    strcpy(comandasv,cmd_final);

    int e_download_req=0;

    if(strncmp(input_buf,"download",8)==0){
        char temp_copy[BUFFER_SIZE];
        strcpy(temp_copy,input_buf);
        strtok(temp_copy," ");

        char *filename=strtok(NULL," ");
        char *path=strtok(NULL," "); // al treilea argument

        if(filename){
            e_download_req=1;
            sprintf(comandasv,"download %s",filename);

            if(path){
                char expanded[BUFFER_SIZE];
                path_extins(path,expanded);
                if(is_directory(expanded)){
                    snprintf(download_path_pending, sizeof(download_path_pending), "%s/%s", expanded, filename);
               } else {
                    strcpy(download_path_pending, expanded);}

            }else{
                strcpy(download_path_pending,filename);
            }
            waiting_for_download = 1; 
                log_to_cmd("Cerere de download trimisa...");
        }else{
            download_path_pending[0] = '\0';
            log_to_cmd("Eroare: Lipseste numele fisierului! Incearca download <file> [path]");
                continue;
        }
    }
    
    int needs_ack = (strncmp(comandasv,"insert",6)==0 || strncmp(comandasv,"delete",6)==0);
    sendMess(sd,comandasv);
    if(needs_ack){
        awaiting_ack=1;
        wlog("Asteapta update de la server...");
    }
}

 
} 


close(sd);
close_ncurses();
return 0; 
}
