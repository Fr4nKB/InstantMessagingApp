#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

#define MAXLEN 30
#define CMDMAXLEN 15+2*MAXLEN
#define MAXMESBUFLEN 1024

int listener;   //sd per il socket di ascolto del peer
int sdserver = -1;  //sd per il socket di comunicazione con il server
int port;   //contiene la porta su cui si trova il peer
int sport = 4242;   //contiene la porta su cui si trova il server, di default 4242
int logged = 0;     //booleano, indica se si e' loggati con il server
int online = 0;     //booleano, indica se si e' connessi al server
int chatOpened = 0;     //booleano, indica se e' aperta una chat
int chatType = 0;       //indica il tipo di chat (0 = invio messaggi al server, 1 = chat diretta con un peer, 2 = chat di gruppo)
int chatConnected = 0;  //booleano, 0 se la chat non e' connessa quindi l'invio dei messaggi avviene tramite il server, 1 se l'invio e' diretto ai peer
int maxfd = STDIN_FILENO;
int usd, dport, ret;    //variabile di appoggio
char username[MAXLEN], pwd[MAXLEN];     //variabile per contenere i dati dell'utente
char FOLDER[MAXLEN+1], currentChat[2*MAXLEN+5], GRUPPO[2*MAXLEN + 11], RUBRICA[42], CPY[2*MAXLEN + 8];    //variabile per contenere il path del file
char *line = NULL, currentChatUname[MAXLEN], uname[MAXLEN];       //variabile di appoggio
uint32_t memTS = 0;     //contiene l'ultimo timestamp di logout memorizzato
size_t len = 0;       //variabile di appoggio
typedef struct sockaddr_in sockaddr_in;
sockaddr_in server;
fd_set masterR;
FILE *chat, *gruppo;       //variabile di appoggio

/*Formato di un messaggio
    type = 0 signup
    type = 1 login
    type = 2 ack
    type = 3 logout
    type = 4 startchat
    type = 5 infochat
    type = 6 msg
    type = 7 hanging/show
    type = 8 file sharing
    type = 9 aggiunta ad un gruppo
*/
struct message {

    uint8_t type;
    uint32_t timestamp;
    char sender[MAXLEN], receiver[MAXLEN];
    char buffer[MAXMESBUFLEN];

} typedef message;

struct peer {

    char uname[MAXLEN];
    int sd, port;
    struct peer *next;

} typedef peer;
peer *peerList = NULL;

/*Restituisce 0 se user e' all'interno della rubrica, -1 altrimenti*/
int checkContact(char *user) {
    FILE *ptr = fopen(RUBRICA, "a+");
    while(getline(&line, &len, ptr) != -1) {
        sscanf(line, "%s\n", uname);
        if(strcmp(uname, user) == 0) {
            fclose(ptr);
            return 0;
        }
    }

    fclose(ptr);
    return -1;

}

void insert(char *user, int sd, int port) {
    peer *tmp = (peer*) malloc(sizeof(peer));
    tmp->sd = sd;
    tmp->port = port;
    strcpy(tmp->uname, user);

    if(peerList == NULL) {
        tmp->next = NULL;
        peerList = tmp;
        return;
    }

    tmp->next = peerList;
    peerList = tmp;
}

int length() {

    if(peerList == NULL) return 0;
    peer *p = peerList;
    int count = 0;

    while(p != NULL) {
        count++;
        p = p->next;
    }

    return count;

}

/*Ricerca un peer all'interno della lista dei peer online in base allo username, restituisce sd e porta*/
int search(char *user, int *prt) {

    if(peerList == NULL) return -1;

    peer *p = peerList;

    while(p != NULL) {
        if(strcmp(p->uname, user) == 0) {
            *prt = p->port;
            return p->sd;
        }
        p = p->next;
    }

    return -1;
}

/*Ricerca un peer all'interno della lista dei peer online in base al socket descriptor*/
peer* searchBySD (int sd) {
    if(peerList == NULL) return NULL;

    peer *p = peerList;

    while(p != NULL) {
        if(p->sd == sd) {
            return p;
        }
        p = p->next;
    }

    return NULL;
}

/*Estrae un peer dalla lista dei peer online*/
peer* extract(int sd) {

    if(peerList == NULL) return NULL;

    peer *p = peerList, *q;

    if(peerList->sd == sd) {
        peerList = peerList->next;
        return p;
    }

    while(p != NULL) {

        if(p->sd == sd) {
            q->next = p->next;
            return p;
        }

        q = p;
        p = p->next;
    }

    return NULL;

}

/*Svuota la lista dei peer e chiude il socket di comunicazione con essi*/
void flushList() {
    if(peerList == NULL) return;

    while(peerList != NULL) {
        close(peerList->sd);
        free((void*) extract(peerList->sd));
    }

}

/*Consente di trovare il file della history della chat*/
void findPath(char *path, char *arg1) {
    strcpy(path, FOLDER);
    strcat(path, arg1);
    strcat(path, ".txt");
}

/*Effettua controlli sulla stringa: se vuota o contiene solo a capo restituisce -1*/
int checkString(char *str) {
    if(strlen(str) == 0) return -1;
    if(strcmp(str, "\n") == 0) return -1;
    if(strcmp(str, " ") == 0) return -1;
    if(strcmp(str, "") == 0) return -1;
    return 0;
}

/*Inizializza il socket di comunicazione con il server e si connette ad esso*/
int socketSetup(int *sd, int port, int type, sockaddr_in *addr) {
    
    if(port != -1) {
        memset(addr, 0, sizeof(sockaddr_in));
        (*addr).sin_family = AF_INET;
        (*addr).sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &(*addr).sin_addr);
    }
    
    *sd = socket(AF_INET, type, 0);
    if(*sd == -1) {
        perror("Impossibile creare un socket di comunicazione, errore");
        return -1;
    }

    if(port == -1) return 0;    //non effettuo la bind perche' il socket non e' di ascolto

    ret = bind(*sd, (struct sockaddr*)addr, sizeof(sockaddr_in));
    if(ret == -1) {
        perror("Utilizzare un'altra porta, errore");
        close(*sd);
        return -1;
    }

    return 0;

}

/*Permette l'invio di messaggi formattati*/
void sendMessage(message m, int sd) {

    send(sd, (void*)&m.type, sizeof(uint8_t), 0);

    m.timestamp = htonl(m.timestamp);
    send(sd, (void*)&m.timestamp, sizeof(uint32_t), 0);

    send(sd, (void*)m.sender, MAXLEN, 0);
    send(sd, (void*)m.receiver, MAXLEN, 0);
    
    uint16_t lmsg;
    int length = strlen(m.buffer);
    if(length < MAXMESBUFLEN) length++;
    lmsg = htons(length);

    send(sd, (void*)&lmsg, sizeof(uint16_t), 0);

    send(sd, (void*)&m.buffer, length, 0);

}

/*Permette la ricezione di messaggi formattati*/
int receiveMessage(message *m, int sd) {

    ret = recv(sd, (void*) &(*m).type, sizeof(uint8_t), MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    ret = recv(sd, (void*) &(*m).timestamp, sizeof(uint32_t), MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    (*m).timestamp = ntohl((*m).timestamp);

    ret = recv(sd, (void*) (*m).sender, MAXLEN, MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    ret = recv(sd, (void*) (*m).receiver, MAXLEN, MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    uint16_t lmsg;
    int length;

    ret = recv(sd, (void*)&lmsg, sizeof(uint16_t), 0);
    if(ret == 0 || ret == -1) return ret;
    length = ntohs(lmsg);

    ret = recv(sd, (void*) (*m).buffer, length, MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    return 1;

}

/*Memorizza un messaggio per inviarlo successivamente*/
void addToSend(uint32_t ts, int type, char *receiver, char *buffer) {
    char file[120];
    strcpy(file, FOLDER);
    strcat(file, username);
    strcat(file, "ToSend.txt");
    FILE *toSend = fopen(file, "a+");
    fprintf(toSend, "%d %d %s %s %s", type, ts, username, receiver, buffer);
    fclose(toSend);
}

/*Invia tutti i messaggi memorizzati*/
void flushToSend() {
    char file[120];
    int usd;
    message m;

    strcpy(file, FOLDER);
    strcat(file, username);
    strcat(file, "ToSend.txt");
    FILE *ptr = fopen(file, "a+");

    while(getline(&line, &len, ptr) != -1) {
        sscanf(line, "%hhd %d %s %s %99[^\n]", &(m.type), &(m.timestamp), m.sender, m.receiver, m.buffer);
        strcat(m.buffer, "\n");
        usd = search(m.receiver, &dport);
        if(usd != -1) sendMessage(m, sdserver);
        else sendMessage(m, sdserver);
    }

    fclose(ptr);
    remove(file);
}

/*Invia un messaggio al server per l'autenticazione*/
void authenticate(uint8_t cmd, char *uname, char *pwd) {
    
    message m;
    m.timestamp = 0;

    //preparazione messaggio da inviare al server
    m.type = cmd;
    strcpy(m.sender, uname);
    char str[6];
    sprintf(str, "%d\n", port);
    strcpy(m.buffer, uname);
    strcat(m.buffer, " ");
    strcat(m.buffer, pwd);
    strcat(m.buffer, " ");
    strcat(m.buffer, str);

    sendMessage(m, sdserver);

}

/*Consente di connettersi al server*/
int connectToServer(int sd, sockaddr_in *addr, int prt) {         //effettua una richiesta di connessione

    memset(addr, 0, sizeof(sockaddr_in));

    (*addr).sin_family = AF_INET;
    (*addr).sin_port = htons(prt);
    inet_pton(AF_INET, "127.0.0.1", &(*addr).sin_addr);

    ret = connect(sd, (struct sockaddr*) addr, sizeof(sockaddr_in));
    if(ret == -1) {
        perror("Errore");
        close(sd);
        return -1;
    }

    return 0;

}

/*Invia tutti e soli i timestamp di logout per aggiornare le entry del registro del server*/
void sendLogout() {
    char file[2*MAXLEN + 1];
    strcpy(file, FOLDER);
    strcat(file, username);
    strcat(file, "ToSend.txt");
    FILE *toSend = fopen(file, "r");
    if(toSend == NULL) return;    //nessun vecchio timestamp

    FILE *cpy = fopen(CPY, "a+");
    char *line = NULL;
    size_t len = 0;
    message m;
    strcpy(m.sender, username);

    while(getline(&line, &len, toSend) != -1) {
        sscanf(line, "%hhd %d %s %s %s", &m.type, &m.timestamp, m.sender, m.receiver, m.buffer);
        if(m.type == 3) {
            sendMessage(m, sdserver);
        }
        else fprintf(cpy, "%s", line);
    }

    fclose(toSend);
    fclose(cpy);
    remove(file);
    rename(CPY, file);

}

/*Permette di riconnettersi al server*/
int reconnect() {
    printf("-------------------\n");
    printf("Tentativo di ristabilire la connessione\n");
    socketSetup(&sdserver, -1, SOCK_STREAM, &server);
    ret = connectToServer(sdserver, &server, sport);
    
    if(ret == -1) {
        printf("Server offline\n");
        printf("-------------------\n");
        return -1;
    }
    else {
        printf("Server online\n");
        printf("-------------------\n");
        FD_SET(sdserver, &masterR);
        if(sdserver > maxfd) maxfd = sdserver;
        online = 1;
        sendLogout();
        memTS = 0;
        authenticate(1, username, pwd);
        flushToSend();
        message tmp;
        receiveMessage(&tmp, sdserver);
        return 0;
    }
}

/*Stampa la lista dei comandi*/
void cmdlist() {
    system("clear");
    printf("******************** BENVENUTO/A ********************\n");
    printf("1)\thanging -> mostra la lista degli utenti che ti hanno contattato quando eri offline\n");
    printf("2)\tshow username -> consente di ricevere i messaggi pendenti dall' utente username\n");
    printf("3)\tchat username -> avvia la chat con l'utente username\n");
    printf("4)\tshare file_name -> invia il file file_name all'utente o agli utenti con i quali si sta chattando\n");
    printf("5)\tout -> disconnessione\n");
}

/*Legge la chat corrente ed invia ack di lettura*/
void readChat() {
    chat = fopen(currentChat, "a+");
    char *line = NULL, msg[MAXMESBUFLEN+2];
    uint32_t timestamp;
    size_t len = 0;

    system("clear");
    while(getline(&line, &len, chat) != -1) {
        sscanf(line, "%d %99[^\n]", &timestamp, msg);
        printf("%s\n", msg);
    }

    fclose(chat);

    if(chatType == 0) chatType = 1;

}

/*Modifica la history dei messaggi portando il messaggio da consegnato * a letto ***/
void updateChat(uint32_t ts, char *file) {

    char *line = NULL, msg[MAXMESBUFLEN+2];
    uint32_t timestamp;
    size_t len = 0;
    FILE *chatcpy = fopen(CPY, "a+");
    chat = fopen(file, "a+");

    while(getline(&line, &len, chat) != -1) {
        sscanf(line, "%d %99[^\n]", &timestamp, msg);
        if(ts == timestamp) fprintf(chatcpy, "%d *%s\n", 0, msg);
        else fprintf(chatcpy, "%d %s\n", timestamp, msg);
    }

    fclose(chat);
    fclose(chatcpy);
    remove(file);
    rename(CPY, file);
    if(strcmp(file, currentChat) == 0) readChat();

}

void exitGroup() {
    message m;
    m.type = 5;
    strcpy(m.sender, username);
    strcpy(m.buffer, "exit\n");

    gruppo = fopen(GRUPPO, "a+");
    while((ret = getline(&line, &len, gruppo)) != -1) {
        sscanf(line, "%s %d\n", uname, &dport);
        usd = search(uname, &dport);
        sendMessage(m, usd);
    }
    fclose(gruppo);
}

/*Chiude la chat attuale*/
void closeChat() {

    if(chatType == 2) exitGroup();

    chatConnected = chatOpened = chatType = 0;

    //reset del file
    remove(GRUPPO);
    gruppo = fopen(GRUPPO, "a+");
    fclose(gruppo);

    memset(&currentChatUname, 0, MAXLEN);
    memset(&currentChat, 0, MAXLEN+4);

}

/*Consente di connettersi ad un peer*/
int connectToPeer(int *usd, sockaddr_in *add, int dport, char *uname) {
    
    ret = socketSetup(usd, -1, SOCK_STREAM, NULL);
    if(ret == -1) return ret;

    ret = connectToServer(*usd, add, dport);
    if(ret == -1) return ret;

    //autenticazione nei confronti del peer, invio username e porta
    message ack;
    ack.type = 2;
    strcpy(ack.sender, username);
    sprintf(ack.buffer, "%d\n", port);
    sendMessage(ack, *usd);

    //inserimento nella lista peer online
    insert(uname, *usd, dport);
    FD_SET(*usd, &masterR);
    if((*usd) > maxfd) maxfd = (*usd);

    return 0;

}

/*Gestisce la chat*/
void Chat(char *buffer) {

    message m, r;
    int usd, dport;
    char *line = NULL, peerName[MAXLEN];
    size_t len = 0;
    sockaddr_in add;

    //preparazione messaggio
    m.timestamp = (uint32_t) time(NULL);
    strcpy(m.buffer, buffer);
    strcpy(m.sender, username);

    if(!chatConnected) {    //primo messaggio, connetto al server ed ottengo la porta del client
        usd = search(currentChatUname, &dport);
        if(usd != -1) { //peer gia' connesso
            chatConnected = chatType = 1;
            gruppo = fopen(GRUPPO, "a+");
            fprintf(gruppo, "%s %d\n", currentChatUname, dport);     //inserimento all'interno del file di gruppo
            fclose(gruppo);
            m.type = 6;
            strcpy(m.receiver, currentChatUname);
            sendMessage(m, usd);
        }
        else {
            if(!online) {   //peer offline occorre inviare al server, se si e' stati disconnessi tenta una riconnessione
                ret = reconnect();
                sleep(2);
                if(ret == -1) return;
            }
            m.type = 4;
            strcpy(m.receiver, currentChatUname);
            sendMessage(m, sdserver);
            chatType = 1;

            ret = receiveMessage(&r, sdserver);
            if(ret == 0 || ret == -1) {
                closeChat(); 
                close(sdserver);
                sdserver = -1;
                online = 0;
                FD_CLR(sdserver, &masterR);
                cmdlist();
                printf("Server offline\n");
                return;
            }

            sscanf(r.buffer, "%d", &dport);
            if(r.type == 5 && dport != -1) {  //peer online
                connectToPeer(&usd, &add, dport, currentChatUname);
                gruppo = fopen(GRUPPO, "a+");
                fprintf(gruppo, "%s %d\n", currentChatUname, dport);     //inserimento all'interno del file di gruppo
                fclose(gruppo);
                chatConnected = chatType = 1;
            }
        }

        if(chatType <= 1) {    //memorizzazione messaggio nella history solo se chat non di gruppo
            chat = fopen(currentChat, "a+");
            fprintf(chat, "%d *%s", m.timestamp, buffer);
            fclose(chat);
        }
    }

    else {      //i peer sono sulla stessa chat, invio messaggi diretto
        m.type = 6;
        if(chatType < 2) {    //memorizzazione messaggio nella history solo se chat non di gruppo
            strcpy(m.receiver, currentChatUname);
            chat = fopen(currentChat, "a+");
            fprintf(chat, "%d *%s", m.timestamp, m.buffer);
            fclose(chat);
        }
        else if(chatType == 2) m.timestamp = -1;

        gruppo = fopen(GRUPPO, "a+");
        while(getline(&line, &len, gruppo) != -1) {   //invio messaggio a tutti i partecipanti
            sscanf(line, "%s %d\n", peerName, &dport);
            usd = search(peerName, &dport);
            if(usd != -1) sendMessage(m, usd);
        }
        fclose(gruppo);
    }

}

/*Invia il file name ad un peer specificando lo username oppure ai peer presenti nel file di gruppo se user == NULL*/
void sendFile(char *name, char *user) {

    message m;
    m.type = 8;
    int usd;
    int i = 0, dim = length();  //ottiene dimensione della lista dei peer online
    int sds[dim+1];     //array che contenga tutti i sd dei peer online

    //controllo che l'invio possa essere fatto
    if(strlen(name) > MAXMESBUFLEN) {
        printf("Nome file troppo lungo\n");
        return;
    }

    FILE *ptr = fopen(name, "rb");
    if(ptr == NULL) {
        printf("Il file specificato non esiste\n");
        return;
    }

    if(user != NULL) {
        usd = search(user, &dport);
        if(usd == -1) {
            printf("Impossibile condividere il file con l'utente specificato, potrebbe essere offline\n");
            fclose(ptr);
            sleep(2);
            return;
        }
    }

    else {
        gruppo = fopen(GRUPPO, "a+");
        while(getline(&line, &len, gruppo) != -1) {
            sscanf(line, "%s %d\n", uname, &dport);
            usd = search(uname, &dport);
            if(usd != -1) {
                sds[i] = usd;
                i++;
            }
        }
        sds[i] = -1;    //l'ultime elemento vale -1, permette di capire quando non ci sono piu' peer a cui inviare
        fclose(gruppo);
    }

    if(i == 0 && sds[i] == -1) {    //nessun peer nel file di gruppo
        printf("Impossibile condividere il file con l'utente, potrebbe essere offline\n");
        fclose(ptr);
        sleep(2);
        return;
    }

    size_t ret_t = 1;
    while(1) {  //legge ed invia al piu' MAXMESBUFLEN byte
        memset(m.buffer, 0, MAXMESBUFLEN);     //pulizia
        ret_t = fread(m.buffer, sizeof(*(m.buffer)), sizeof(m.buffer)/sizeof(*(m.buffer)), ptr);
        if(ret_t == 0 || ret_t == EOF) break;     //EOF
        m.timestamp = ret_t;       //popola con il numero di byte letti
        if(user == NULL) {
            i = 0;
            while(sds[i] != -1 && i <= dim) {   //invio a tutti peer del gruppo
                sendMessage(m, sds[i]);
                i++;
            }
        }
        else sendMessage(m, usd);   //invio al peer specificato
    }
    fclose(ptr);

    //invio messaggio fine sharing
    m.type = 2;
    memset(m.buffer, 0, MAXMESBUFLEN);
    if(user == NULL) {
        i = 0;
        while(sds[i] != -1 && i <= dim) {
            sendMessage(m, sds[i]);
            i++;
        }
    }
    else sendMessage(m, usd);

}

/*Consente di ricevere un file*/
void receiveFile(message m, int sd) {

    char file[MAXMESBUFLEN];
    strcpy(file, FOLDER);
    strcat(file, m.buffer);
    
    FILE *ptr = fopen(file, "wb");

    while(1) {
        ret = receiveMessage(&m, sd);
        if(ret == -1) {
            fclose(ptr);
            remove(file);
            return;
        }
        else if(ret == 0) {
            fclose(ptr);
            remove(file);
            peer *p = extract(sd);
            if(p != NULL) free(p);
            close(sd);
            FD_CLR(sd, &masterR);
            return;
        }
        else if(m.type == 2) break;

        fwrite(m.buffer, m.timestamp, 1, ptr);

    }

    fclose(ptr);

}

/*Permette di aggiungere un utente alla chat attuale*/
void addUser() {

    if(!online) {
        if(reconnect() == -1) {
            sleep(2);
            return;
        }
    }

    //richiesta lista peer online
    message m;
    m.type = 5;
    strcpy(m.sender, username);
    sendMessage(m, sdserver);

    receiveMessage(&m, sdserver);
    if(ret == 0) {
        FD_CLR(sdserver, &masterR);
        close(sdserver);
        sdserver = -1;
        online = 0;
        printf("Server offline\n");
        return;
    }
    else if(ret == -1) return;

    printf("-------------------\n");

    char file[MAXMESBUFLEN];
    strcpy(file, FOLDER);
    strcat(file, m.buffer);
    receiveFile(m, sdserver);       //ricezione file con utenti attualmente online

    char *line = NULL, uname[MAXLEN], buffer[MAXLEN+3], choice[2], toAdd[MAXLEN];
    size_t len = 0;
    int dport, prt;
    FILE *ptr = fopen(file, "r");

    while(1) {
        printf("Scrivi \\a username per aggiungere un utente fra quelli nella lista nel gruppo\n");
        printf("Altrimenti \\a per annullare l'operazione di aggiunta\n");
        while(getline(&line, &len, ptr) != -1) {
            sscanf(line, "%s %d\n", uname, &dport);
            if(strcmp(uname, username) != 0) printf("%s\n", uname);
        }

        rewind(ptr);
        memset(buffer, 0, MAXLEN+3);
        read(STDIN_FILENO, buffer, MAXLEN+3);
        sscanf(buffer, "%2s %30s\n", choice, toAdd);
        if(strcmp(choice , "\\a") == 0) break;
    }

    fflush(stdin);
    int count = 0;  //variabile per contare utenti nella lista degli utenti online

    while((ret = getline(&line, &len, ptr)) != -1) {
        sscanf(line, "%s %d\n", uname, &dport);
        count++;
        if(strcmp(toAdd, uname) == 0 && strcmp(toAdd, username) != 0) break;    //controlla che sia nella lista e che non sia l'utente stesso
    }

    fclose(ptr);
    if(count == 0) {
        printf("Nessun utente online\n");
        printf("-------------------\n");
        return;
    }
    else if(strlen(toAdd) == 0) {
        printf("-------------------\n");
        return;
    }
    else if(ret == -1){
        printf("Impossibile aggiungere l'utente selezionato\n");
        printf("-------------------\n");
        return;
    }

    ptr = fopen(GRUPPO, "a+");      //controlla che l'utente selezionato non sia gia' presente
    while((ret = getline(&line, &len, ptr)) != -1) {
        sscanf(line, "%s %d\n", uname, &prt);
        if(strcmp(toAdd, uname) == 0) {
            fclose(ptr);
            printf("Utente gia' presente\n");
            printf("-------------------\n");
            return;
        }
    }
    fclose(ptr);

    int usd, ris;
    sockaddr_in add;
    if((usd = search(toAdd, &prt)) == -1) ris = connectToPeer(&usd, &add, dport, toAdd);
    if(ris == -1) {
        printf("Impossibile connettersi con l'utente specificato\n");
        printf("-------------------\n");
        return;
    }

    if(chatType < 2) system("clear");   //se la chat diventa di gruppo rimuove i messaggi precedenti
    chatType = 2;

    //invio lista dei peer che partecipano al gruppo
    m.type = 9;
    strcpy(m.sender, username);
    strcpy(m.buffer, "newGruppo.txt");
    sendMessage(m, usd);
    sendFile(GRUPPO, toAdd);

    //aggiunta peer al gruppo
    gruppo = fopen(GRUPPO, "a+");
    fprintf(gruppo, "%s %d\n", toAdd, dport);
    fclose(gruppo);
    chatConnected = 1;

    printf("%s e' stato aggiunto al gruppo\n", toAdd);

    printf("-------------------\n");

}

/*Rimuove il timestamp di logout dal file ToSend con il timestamp ts*/
void removeLogout(uint32_t ts) {
    char file[2*MAXLEN + 1];
    strcpy(file, FOLDER);
    strcat(file, username);
    strcat(file, "ToSend.txt");
    FILE *toSend = fopen(file, "r");
    if(toSend == NULL) return;    //nessun vecchio timestamp

    FILE *cpy = fopen(CPY, "a+");
    char *line = NULL;
    size_t len = 0;
    message m;

    while(getline(&line, &len, toSend) != -1) {
        sscanf(line, "%hhd %d %s %s %s", &m.type, &m.timestamp, m.sender, m.receiver, m.buffer);
        if(!(m.type == 3 && m.timestamp == ts)) fprintf(cpy, "%s", line);
    }

    fclose(toSend);
    fclose(cpy);
    remove(file);
    rename(CPY, file);

}

/*Gestisce l'operazione di signup e login*/
int login() {
    char choice[7], arg1[MAXLEN+1], arg2[MAXLEN+1], arg3[MAXLEN+1], cmd[CMDMAXLEN];
    int valid;

    while(1) {
        printf("1)\tsignup username password -> permette di registrarsi\n");
        printf("2)\tin serverprt username password -> permette di autenticarsi\n");
        read(STDIN_FILENO, cmd, CMDMAXLEN);
        arg1[0] = arg2[0] = arg3[0] = '\0';
        sscanf(cmd, "%6s %30s %30s %30s\n", choice, arg1, arg2, arg3);
        valid = 0;

        if(strcmp(choice, "signup") == 0) {
            if(checkString(arg1) == -1 || checkString(arg2) == -1) {
                printf("Inserire almeno un carattere\n");
                sleep(1);
                system("clear");
                continue;
            }
            if(!online) {
                ret = connectToServer(sdserver, &server, 4242);
                if(ret == -1) exit(0);
                else if(ret == 0) online = 1;
            }
            strcpy(username, arg1); strcpy(pwd, arg2);
            authenticate(0, arg1, arg2);
            valid = 1;     //e' stato digitato un comando valido, aspetto la risposta dal server
        }

        else if(strcmp(choice, "in") == 0) {
            if(checkString(arg2) == -1 || checkString(arg3) == -1) {
                printf("Inserire almeno un carattere\n");
                sleep(1);
                system("clear");
                continue;
            }

            sport = atoi(arg1);
            if(sport < 1024 || sport > 65535) {
                printf("Porta inserita non valida\n");
                sleep(1);
                system("clear");
                continue;
            }
            if(!online) {
                ret = connectToServer(sdserver, &server, sport);
                if(ret == -1) exit(0);
                else if(ret == 0) online = 1;
            }
            strcpy(username, arg2); strcpy(pwd, arg3);
            authenticate(1, arg2, arg3);
            valid = 1;
        }

        if(valid == 1) {
            message m;
            ret = receiveMessage(&m, sdserver); 

            if(ret == 0) {
                printf("Il server e' offline, impossibile connettersi\n");
                close(listener);
                exit(0);
            }
            else if(ret == -1) {
                printf("Errore nell'invio dei dati, riprovare\n");
                close(sdserver);
                sdserver = -1;
                online = 0;
                close(listener);
                exit(0);
            }

            if(m.type == 2) {
                if(strcmp(m.buffer, "1\n") == 0) {      //signup o login effettuato con successo
                    
                    //popolamento file e directory di utility
                    strcpy(FOLDER, username);
                    strcat(FOLDER, "/");

                    strcpy(GRUPPO, FOLDER);
                    strcpy(CPY, FOLDER);
                    strcpy(RUBRICA, FOLDER);
                    strcat(GRUPPO, "Gruppo.txt");
                    strcat(CPY, "CPY.txt");
                    strcat(RUBRICA, "Rubrica.txt");

                    remove(GRUPPO);     //eventuale pulizia del file se gia' esistente

                    //crea cartella per l'utente ed abilita lettura, scrittura ed esecuzione
                    mkdir(username, 0700);

                    //invio eventuale timestamp di logout precedenti
                    if(strcmp(choice, "in") == 0) sendLogout();
                    return 0;
                }
                else return -1;
            }
        }

        else {
            printf("Comando non valido...\n");
            sleep(1);
            system("clear");
        }
    }

}

/*Gestisce la terminazione del processo dovuta ad un segnale di SIGTERM*/
void handler(int sig) {
    if(sig == SIGTERM) {
        flushList();
        close(listener);
        if(online) close(sdserver);
        exit(0);
    }    
}

int main(int argc, char** argv) {

    signal(SIGTERM, handler);

    int i, sdclient;
    fd_set readers;
    sockaddr_in addr, client;
    message m;
    socklen_t socklen;
    struct timeval timeout;
    char choice[9], arg1[MAXLEN], arg2[MAXLEN], arg3[MAXLEN], buffer[MAXMESBUFLEN];

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    FD_SET(STDIN_FILENO, &masterR);

    system("clear");
    if(argc > 2) {
        printf("Numero di parametri eccessivo, inserire al piu' un parametro\n");
        return -1;
    }
    else if(argc == 2) port = atoi(argv[1]);
    else if (argc < 2) {
        printf("Fornire una porta\n");
        return -1;
    }
    if(port < 1024 || port > 65535) {
        printf("La porta selezionata non e' valida\n");
        return -1;
    }

    if(socketSetup(&listener, port, SOCK_STREAM, &addr) == -1) return -1;  //creazione socket di ascolto
    listen(listener, 10);
    if(socketSetup(&sdserver, -1, SOCK_STREAM, &server) == -1) return -1;  //creazione socket di comunicazione con server

    do {
        ret = login();
        if(ret == -1) {
            system("clear");
            printf("Username o password sbagliati...\n");
        }
    } while(ret == -1);

    flushToSend();
    cmdlist();
    
    FD_SET(listener, &masterR);
    FD_SET(sdserver, &masterR);
    if(listener > maxfd) maxfd = listener;
    if(sdserver > maxfd) maxfd = sdserver;

    while(1) {
        readers = masterR;
        select(maxfd+1, &readers, NULL, NULL, &timeout);
        for(i = 0; i < maxfd+1; i++) {
            if(FD_ISSET(i, &readers)) {

                if(i == listener) {     //ricevuta una nuova richiesta
                    socklen = sizeof(client);
                    sdclient = accept(listener, (struct sockaddr*) &client, &socklen);
                    FD_SET(sdclient, &masterR);

                    ret = receiveMessage(&m, sdclient);     //invio dati da parte del peer appena connesso
                    if(ret == 0 || ret == -1) {
                        FD_CLR(sdclient, &masterR);
                        close(sdclient);
                        continue;
                    }

                    if(m.type == 2) {   //aggiornamento lista dei peer a cui si e' connessi
                        int dport;
                        sscanf(m.buffer, "%d\n", &dport);
                        insert(m.sender, sdclient, dport);
                    }

                    if(sdclient > maxfd) maxfd = sdclient;

                    printf("L'utente %s si e' appena connesso\n", m.sender);

                }

                else if(i == STDIN_FILENO) {
                    memset(buffer, 0, MAXMESBUFLEN);
                    read(STDIN_FILENO, buffer, MAXMESBUFLEN);
                    sscanf(buffer, "%8s %30s %30s %30s\n", choice, arg1, arg2, arg3);

                    if(chatOpened) {    //una chat e' aperta, invio il messaggio

                        if(strcmp(buffer, "\\q\n") == 0) {
                            closeChat();
                            cmdlist();
                            continue;
                        }

                        else if(strcmp(buffer, "\\u\n") == 0) { //richiesta al server degli utenti online
                            addUser();
                        }

                        else if(strcmp(choice, "share") == 0) {

                            char file[120];
                            strcpy(file, FOLDER);
                            strcat(file, arg1);
                            FILE *ptr = fopen(file, "rb");
                            if(ptr == NULL) {
                                printf("Il file specificato non esiste\n");
                                continue;
                            }
                            fclose(ptr);

                            m.type = 8;
                            strcpy(m.sender, username);
                            strcpy(m.buffer, arg1);
                            
                            ptr = fopen(GRUPPO, "a+");
                            while(getline(&line, &len, ptr) != -1) {
                                sscanf(line, "%s %d\n", uname, &dport);
                                usd = search(uname, &dport);
                                sendMessage(m, usd);
                            }
                            fclose(ptr);
                            
                            printf("-------------------\n");
                            printf("Inizio condivisione %s\n", file);
                            sendFile(file, NULL);
                            printf("File condiviso\n");
                            printf("-------------------\n");
                            sleep(1);
                            if(chatType < 2) readChat();
                        }

                        else {
                            if(strlen(buffer) == 0) continue;
                            if(strcmp(buffer, "\n") == 0) continue;
                            if(strcmp(buffer, " ") == 0) continue;
                            if(strcmp(buffer, "") == 0) continue;
                            Chat(buffer);
                            if(chatType < 2) readChat();
                        }
                    }

                    else {  //nessuna chat aperta

                        sscanf(buffer, "%8s %30s %30s %30s\n", choice, arg1, arg2, arg3);
                    
                        if(strcmp(choice, "hanging") == 0) {
                            if(!online) {
                                if(reconnect() == -1) {
                                    sleep(2);
                                    continue;
                                }
                            }
                            m.type = 7;
                            strcpy(m.sender, username);
                            strcpy(m.buffer, "list\n");
                            sendMessage(m, sdserver);

                            system("clear");
                            while(1) {
                                ret = receiveMessage(&m, sdserver);
                                if(ret == 0 || ret == -1) {
                                    FD_CLR(sdserver, &masterR);
                                    close(sdserver);
                                    sdserver = -1;
                                    online = 0;
                                    printf("Server offline\n");
                                    continue;
                                }

                                if(m.type == 7) {
                                    struct tm ts;
                                    long t = (long) m.timestamp;
                                    memcpy(&ts, localtime(&t), sizeof(struct tm));   //conversione secondi da epoch a formato leggibile
                                    printf("%s %s %d:%d:%d\n", m.sender, m.buffer, ts.tm_hour, ts.tm_min, ts.tm_sec);
                                }
                                else if(m.type == 2) break;

                            }
                            
                            printf("Premere invio per continuare...\n");
                            fflush(stdin);
                            while(getchar() != '\n');
                            cmdlist();
                           
                        }

                        else if(strcmp(choice, "show") == 0) {
                            if(strcmp(choice, username) == 0) {
                                printf("Impossibile eseguire show su stessi\n");
                                continue;
                            }
                            if(!online) {
                                if(reconnect() == -1) {
                                    sleep(2);
                                    continue;
                                }
                            }
                            m.type = 7;
                            strcpy(m.sender, username);
                            strcpy(m.receiver, arg1);
                            strcpy(m.buffer, "send\n");
                            sendMessage(m, sdserver);
                            system("clear");
                            cmdlist();
                        }
                    
                        else if(strcmp(choice, "chat") == 0) {

                            if(strcmp(username, arg1) == 0) {
                                printf("Impossibile avviare una chat con se stessi\n");
                                continue;
                            }
                            
                            if(checkContact(arg1) == -1) {
                                printf("Impossibile avviare una chat con un utente che non e' presente nella rubrica\n");
                                continue;
                            }

                            chatOpened = 1;

                            strcpy(currentChatUname, arg1);
                            findPath(currentChat, arg1);
                            chat = fopen(currentChat, "a+");
                            gruppo = fopen(GRUPPO, "a+");
                            if(search(currentChatUname, &dport) != -1) {
                                fprintf(gruppo, "%s %d\n", currentChatUname, dport);
                                chatConnected = chatType = 1;
                            }
                            fclose(chat);
                            fclose(gruppo);

                            readChat();

                        }

                        else if(strcmp(choice, "out") == 0) {
                            system("clear");
                            if(online) close(sdserver);
                            else {
                                if(memTS != 0) removeLogout(memTS);         //aggiornamento vecchio timestamp
                                addToSend((uint32_t) time(NULL), 3, "", "\n");
                            }
                            close(listener);
                            flushList();
                            exit(0);
                        }
                            
                        else {
                            printf("Comando non valido...\n");
                        }

                    }

                }

                else if(i == sdserver) {

                    ret = receiveMessage(&m, i);
                    if(ret == 0) {
                        FD_CLR(sdserver, &masterR);
                        close(sdserver);
                        sdserver = -1;
                        online = 0;
                        memTS = (uint32_t) time(NULL);
                        addToSend(memTS, 3, "", "\n");
                        printf("Server offline\n");
                        continue;
                    }

                    char file[5 + 2*MAXLEN];
                    findPath(file, m.sender);

                    if(m.type == 2 && strcmp(m.buffer, "1\n") == 0) {   //messaggio letto
                        printf("Ricevuto un ack di lettura dal server\n");
                        updateChat(m.timestamp, file);
                    }

                    else if(m.type == 4) {   //ricevuto un messaggio di avvio chat

                        FILE *tmp = fopen(file, "a+");
                        fprintf(tmp, "%d >%s", m.timestamp, m.buffer);
                        fclose(tmp);
                        message ack;
                        ack.type = 2;
                        ack.timestamp = m.timestamp;
                        strcpy(ack.sender, username);
                        strcpy(ack.receiver, m.sender);
                        strcpy(ack.buffer, "1\n");
                        sendMessage(ack, sdserver);

                        if(chatType < 2 && strcmp(file, currentChat) == 0) {    //peer nella stessa chat
                            readChat();
                        }

                        else printf("Ricevuto un messaggio da %s\n", m.sender);

                    }

                }

                else {  //messaggio ricevuto da un altro peer

                    ret = receiveMessage(&m, i);
                    if(ret == 0) {
                        peer* p = extract(i);
                        printf("L'utente %s si e' disconesso\n", p->uname);
                        FILE *ptr = fopen(GRUPPO, "a+");
                        FILE *cpy = fopen(CPY, "a+");
                        while(getline(&line, &len, ptr) != -1) {    //rimozione peer dal file gruppo se all'interno
                            sscanf(line, "%s %d\n", uname, &dport);
                            if(strcmp(p->uname, uname) != 0) fprintf(cpy, "%s", line);
                            else {
                                if(chatType < 2) chatConnected = 0;
                                printf("Utente rimosso dal gruppo\n");
                            }
                        }
                        fclose(ptr);
                        fclose(cpy);
                        remove(GRUPPO);
                        rename(CPY, GRUPPO);
                        
                        free(p);
                        close(i);
                        FD_CLR(i, &masterR);
                        continue;
                    }

                    else if (ret == -1) continue;

                    else if(m.type == 2 && strcmp(m.buffer, "1\n") == 0) {   //messaggio letto
                        char file[5 + 2*MAXLEN];
                        findPath(file, m.sender);
                        updateChat(m.timestamp, file);
                    }

                    else if(m.type == 5) {  //un peer e' stato aggiunto/rimosso al/dal gruppo da un altro peer

                        int dport;
                        char uname[MAXLEN], *line = NULL;
                        size_t len = 0;

                        //il peer e' uscito dal gruppo, occorre aggiornare il file
                        if(strcmp(m.buffer, "exit\n") == 0) {
                            gruppo = fopen(GRUPPO, "a+");
                            FILE *cpy = fopen(CPY, "a+");
                            while((ret = getline(&line, &len, gruppo)) != -1) {
                                sscanf(line, "%s %d\n", uname, &dport);
                                if(strcmp(uname, m.sender) != 0) fprintf(cpy, "%s", line);
                                else {
                                    printf("-------------------\n");
                                    printf("%s e' uscito dal gruppo\n", m.sender);
                                    printf("-------------------\n");
                                }
                            }
                            fclose(gruppo);
                            fclose(cpy);
                            remove(GRUPPO);
                            rename(CPY, GRUPPO);
                            continue;

                        }

                        if(chatType < 2) system("clear");   //il peer non e' all'interno di un gruppo
                        printf("-------------------\n");

                        char adder[MAXLEN], added[MAXLEN];
                        int adderPRT, addedPRT;
                        sscanf(m.buffer, "%s %d\n%s %d\n", adder, &adderPRT, added, &addedPRT);

                        //controllo se l'aggiunta e sul gruppo attuale
                        gruppo = fopen(GRUPPO, "a+");
                        while(1) {
                            ret = getline(&line, &len, gruppo);
                            if(ret == -1) break;
                            sscanf(line, "%s %d\n", uname, &dport);
                            if(strcmp(uname, adder) == 0) break;
                        }

                        if(ret == -1) { //il peer che ha aggiunto non e' nel gruppo attuale, e' un nuovo gruppo
                            system("clear");
                            printf("-------------------\n");
                            printf("%s ti ha aggiunto ad un gruppo\n", adder);
                            printf("%s e' stato aggiunto al gruppo\n", added);
                            printf("-------------------\n");
                            fclose(gruppo);
                            remove(GRUPPO);
                            gruppo = fopen(GRUPPO, "a+");
                            usd = search(m.sender, &dport);
                            fprintf(gruppo, "%s", m.buffer);
                            fclose(gruppo);
                            chatType = 2;
                            chatOpened = chatConnected = 1;
                            continue;
                        }

                        if(chatType < 2) printf("%s ti ha aggiunto ad un gruppo\n", adder);
                        chatType = 2;
                        chatOpened = chatConnected = 1;
                        
                        //aggiunta peer al gruppo
                        fprintf(gruppo, "%s %d\n", added, adderPRT);
                        fclose(gruppo);

                        printf("%s e' stato aggiunto al gruppo\n", added);
                        printf("-------------------\n");

                    }

                    else if(m.type == 6) {   //ricevuto un messaggio da un peer
                        
                        char file[5 + 2*MAXLEN];
                        findPath(file, m.sender);

                        if(m.timestamp != -1) { //chat singola
                            chat = fopen(file, "a+");
                            fprintf(chat, "%d >%s", m.timestamp, m.buffer);
                            fclose(chat);
                            message ack;
                            ack.type = 2;
                            ack.timestamp = m.timestamp;
                            strcpy(ack.sender, username);
                            strcpy(ack.receiver, m.sender);
                            strcpy(ack.buffer, "1\n");
                            sendMessage(ack, i);    //invio ack di consegna all'utente

                            if(chatType < 2 && strcmp(file, currentChat) == 0) {    //peer nella stessa chat
                                readChat();
                            }
                            else printf("Ricevuto un messaggio da %s\n", m.sender);
                        }
                        else {  //chat di gruppo
                            gruppo = fopen(GRUPPO, "a+");
                            while((ret = getline(&line, &len, gruppo)) != -1) {
                                sscanf(line, "%s %d\n", uname, &dport);
                                if(strcmp(uname, m.sender) == 0) {
                                    printf("%s: %s", m.sender, m.buffer);
                                    break;
                                }
                            }
                            fclose(gruppo);
                            if(ret == -1) { //ricevuto un messaggio di gruppo da un utente che non fa parte del gruppo attuale
                                m.type = 5;
                                strcpy(m.sender, username);
                                strcpy(m.buffer, "exit\n");
                                sendMessage(m, i);
                                continue;
                            }
                        }

                    }

                    else if(m.type == 8) {
                        if(strcmp(m.sender, "\n") != 0) printf("-------------------\n%s sta condividendo il file %s\n", m.sender, m.buffer);
                        receiveFile(m, i);
                        if(strcmp(m.sender, "\n") != 0) printf("-------------------\nIl file e' stato condiviso\n");
                    }

                    else if(m.type == 9) {  //peer aggiunto ad un gruppo

                        char *line = NULL, uname[MAXLEN];
                        size_t len = 0;
                        sockaddr_in add;
                        int usd, dport, prt;
                        message r;

                        //eventuale comunicazione ai partecipanti al gruppo dell'uscita
                        if(chatType == 2) exitGroup();

                        system("clear");
                        closeChat();
                        chatOpened = chatConnected = 1;
                        chatType = 2;   //imposta la chat a chat di gruppo

                        printf("-------------------\n");
                        printf("%s ti ha aggiunto ad un gruppo\n", m.sender);
                        printf("-------------------\n");

                        receiveFile(m, i);  //ricezione file con i partecipanti al gruppo
                        remove(GRUPPO);
                        char file[60];
                        strcpy(file, FOLDER);
                        strcat(file, m.buffer);
                        rename(file, GRUPPO);   //sostituzione del vecchio file di gruppo con quello nuovo

                        r.type = 5;     //occorre inviare un messaggio ai partecipanti del gruppo per comunicare a tutti dell'aggiunta del nuovo peer
                        strcpy(r.sender, username);
                        search(m.sender, &dport);
                        sprintf(r.buffer, "%s %d\n%s %d\n", m.sender, dport, username, port);   //il peer aggiunto deve comunicare username e porta a tutti gli altri partecipanti del gruppo
                        
                        FILE *ptr = fopen(GRUPPO, "a+");
                        while(getline(&line, &len, ptr) != -1) {
                            sscanf(line, "%s %d\n", uname, &dport);
                            usd = search(uname, &prt);
                            if(usd == -1) ret = connectToPeer(&usd, &add, dport, uname);       //connessione al peer se non si e' gia' connessi
                            if(ret != -1 || usd != -1) {    //invio messaggio per aggiunta al file di gruppo solamente se la connessione con il peer e' stabilita
                                sendMessage(r, usd);        
                            }
                        }

                        rewind(ptr);
                        search(m.sender, &dport);
                        fprintf(ptr, "%s %d\n", m.sender, dport);   //aggiunta al file di gruppo dell'utente che ha invocato addUser
                        fclose(ptr);

                    }

                }

            }
        }
    }
}