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

#define MAXLEN 30
#define CMDMAXLEN 15+2*MAXLEN
#define MAXMESBUFLEN 1024

int listener, sdserver = -1, port, logged = 0, online = 0, chatConnected = 0, chatOpened = 0, grpCount = 0, maxfd = STDIN_FILENO;
char username[MAXLEN], currentChat[MAXLEN+4], currentChatUname[MAXLEN], GRUPPO[MAXLEN + 10], GRUPPOCPY[MAXLEN + 13], CHATCPY[MAXLEN + 7];
typedef struct sockaddr_in sockaddr_in;
sockaddr_in server;
fd_set masterR;
FILE *chat, *gruppo;

struct message {

    uint8_t type;
    uint32_t timestamp;
    char sender[MAXLEN], receiver[MAXLEN];
    char buffer[MAXMESBUFLEN];

} typedef message;
/*
type = 0 signup
type = 1 login
type = 2 ack
type = 3 logout
type = 4 startchat
type = 5 infochat
type = 6 msg

*/

struct peer {

    char uname[MAXLEN];
    int sd, port;
    struct peer *next;

} typedef peer;
peer *peerList = NULL;

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

void flushList() {
    if(peerList == NULL) return;

    while(peerList != NULL) {
        close(peerList->sd);
        free((void*) extract(peerList->sd));
    }

}

void handler(int sig) {
    if(sig == SIGTERM) {
        flushList();
        close(listener);
        if(online) close(sdserver);
    }
    exit(0);
}

void cleantstdin() {
    char c;
    do {
        c = getchar();
    } while (c != '\n' && c != EOF);
}

int socketSetup(int *sd, int port, int type, sockaddr_in *addr) {      //inizializza il socket di comunicazione con il server e si connette ad esso

    int ret;
    
    if(port != -1) {
        memset(addr, 0, sizeof(sockaddr_in));
        (*addr).sin_family = AF_INET;
        (*addr).sin_port = htons(port);
        inet_pton(AF_INET, "10.0.2.15", &(*addr).sin_addr);
    }
    
    *sd = socket(AF_INET, type, 0);
    if(*sd == -1) {
        perror("Impossibile creare un socket di comunicazione, errore: ");
        return -1;
    }

    if(port == -1) return 0;    //non effettuo la bind perche' il socket non e' di ascolto

    ret = bind(*sd, (struct sockaddr*)addr, sizeof(sockaddr_in));
    if(ret == -1) {
        perror("Utilizzare un'altra porta, errore: ");
        close(*sd);
        return -1;
    }

    return 0;

}

void sendMessage(message m, int sd) {

    send(sd, (void*)&m.type, sizeof(uint8_t), 0);

    m.timestamp = htonl(m.timestamp);
    send(sd, (void*)&m.timestamp, sizeof(uint32_t), 0);

    send(sd, (void*)m.sender, MAXLEN, 0);
    send(sd, (void*)m.receiver, MAXLEN, 0);
    
    send(sd, (void*)&m.buffer, MAXMESBUFLEN, 0);

}

int receiveMessage(message *m, int sd) {

    int ret;

    ret = recv(sd, (void*) &(*m).type, sizeof(uint8_t), MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    ret = recv(sd, (void*) &(*m).timestamp, sizeof(uint32_t), MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    (*m).timestamp = ntohl((*m).timestamp);

    ret = recv(sd, (void*) (*m).sender, MAXLEN, MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    ret = recv(sd, (void*) (*m).receiver, MAXLEN, MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    ret = recv(sd, (void*) (*m).buffer, MAXMESBUFLEN, MSG_WAITALL);
    if(ret == 0 || ret == -1) return ret;

    return 1;

}

void authenticate(uint8_t cmd, char *uname, char *pwd) {
    
    message m;
    m.timestamp = 0;
    char *string = m.buffer;

    //preparazione messaggio da inviare al server
    m.type = cmd;
    char str[6];
    sprintf(str, "%d\n", port);
    strcpy(string, uname);
    strcat(string, " ");
    strcat(string, pwd);
    strcat(string, " ");
    strcat(string, str);

    sendMessage(m, sdserver);

}

int connectToServer(int sd, sockaddr_in *addr, char *ind, int prt) {         //consente di connettersi ad un server

    memset(addr, 0, sizeof(sockaddr_in));

    (*addr).sin_family = AF_INET;
    (*addr).sin_port = htons(prt);
    inet_pton(AF_INET, ind, &(*addr).sin_addr);

    int ret = connect(sd, (struct sockaddr*) addr, sizeof(sockaddr_in));
    if(ret == -1) {
        perror("Errore: ");
        close(sd);
        return -1;
    }

    return 0;

}

void cmdlist() {
    system("clear");
    printf("******************** Benvenuto/a ********************\n");
    printf("1)\thanging -> mostra la lista degli utenti che ti hanno contattato quando eri offline\n");
    printf("2)\tshow username -> consente di ricevere i messaggi pendenti dall' utente username\n");
    printf("3)\tchat username -> avvia la chat con l'utente username\n");
    printf("4)\tshare file_name -> invia il file file_name all'utente o agli utenti con i quali si sta chattando\n");
    printf("5)\tout -> disconnessione\n");
}

void addUser() {

}

void closeChat() {

    chatConnected = chatOpened = grpCount = 0;

    //reset del file
    remove(GRUPPO);
    gruppo = fopen(GRUPPO, "a+");
    fclose(gruppo);

    memset(&currentChatUname, 0, MAXLEN);
    memset(&currentChat, 0, MAXLEN+4);

    cmdlist();

}

void readChat() {

    chat = fopen(currentChat, "a+");
    FILE *tmp = fopen(CHATCPY, "a+");
    char *line = NULL, msg[MAXMESBUFLEN+2], c;
    uint32_t timestamp;
    size_t len = 0;

    system("clear");
    while(getline(&line, &len, chat) != -1) {
        sscanf(line, "%d %99[^\n]", &timestamp, msg);
        sscanf(msg, "%1c", &c);

        if(c == '>') {

            sscanf(msg, "%1c%99[^\n]", &c, msg);

            message ack;
            ack.type = 2;
            ack.timestamp = timestamp;
            strcpy(ack.sender, username);
            strcpy(ack.receiver, currentChatUname);
            strcpy(ack.buffer, "1\n");
            int dport, usd = search(ack.receiver, &dport);

            if(chatConnected && grpCount == 1 && usd != -1) sendMessage(ack, usd);   //se il peer e' online invia ack diretto
            else if(sdserver != -1) sendMessage(ack, sdserver);   //altrimenti al server
            else {  //se il server e' offline bufferizza per inviare piu' tardi
                char nome[MAXLEN + 4];
                strcpy(nome, username);
                strcat(nome, "ToSend.txt");
                FILE *toSend = fopen(nome, "a+");
                fprintf(toSend, "%d %d %s %s %s", ack.type, ack.timestamp, ack.sender, ack.receiver, ack.buffer);
                fclose(toSend);
            }
        }

        fprintf(tmp, "%d %s\n", timestamp, msg);
        printf("%s\n", msg);
    }

    fclose(chat);
    fclose(tmp);
    remove(currentChat);
    rename(CHATCPY, currentChat);

}

void updateChat(uint32_t ts) {

    char *line = NULL, msg[MAXMESBUFLEN+2];
    uint32_t timestamp;
    size_t len = 0;
    FILE *chatcpy = fopen(CHATCPY, "a+");
    chat = fopen(currentChat, "a+");

    while(getline(&line, &len, chat) != -1) {
        sscanf(line, "%d %99[^\n]", &timestamp, msg);
        if(ts == timestamp) fprintf(chatcpy, "%d *%s\n", timestamp, msg);
        else fprintf(chatcpy, "%d %s\n", timestamp, msg);
    }

    fclose(chat);
    fclose(chatcpy);
    remove(currentChat);
    rename(CHATCPY, currentChat);
    readChat();

}

void Chat(char *buffer) {

    message m, r;
    int usd, dport, ret;
    char *line = NULL, peerName[MAXLEN];
    size_t len = 0;
    sockaddr_in add;

    m.timestamp = (uint32_t) time(NULL);
    strcpy(m.buffer, buffer);
    strcpy(m.sender, username);
    chat = fopen(currentChat, "a+");
    fprintf(chat, "%d *%s", m.timestamp, m.buffer);   //memorizzazione messaggio nella history
    fclose(chat);

    if(!chatConnected) {    //primo messaggio, connetto al server ed ottengo la porta del client
        usd = search(currentChatUname, &dport);
        if(usd != -1) { //peer gia' connesso
            grpCount++;
            chatConnected = 1;
            gruppo = fopen(GRUPPO, "a+");
            fprintf(gruppo, "%s\n", currentChatUname);     //memorizzazione dati peer
            fclose(gruppo);
            m.type = 6;
            strcpy(m.receiver, currentChatUname);
            sendMessage(m, usd);
            return;
        }
        else {
            m.type = 4;
            strcpy(m.receiver, currentChatUname);
            sendMessage(m, sdserver);

            ret = receiveMessage(&r, sdserver);
            if(ret == 0 || ret == -1) {
                closeChat();
                close(sdserver);
                sdserver = -1;
                online = 0;
                FD_CLR(sdserver, &masterR);
                return;
            }

            sscanf(r.buffer, "%d", &dport);
            if(r.type == 5 && dport != -1) {  //peer online
                
                ret = socketSetup(&usd, -1, SOCK_STREAM, NULL);
                if(ret == -1) {
                    closeChat();
                    return;
                }

                ret = connectToServer(usd, &add, "10.0.2.15", dport);
                if(ret == -1) {
                    closeChat();
                    return;
                }

                message ack;
                ack.type = 2;
                strcpy(ack.sender, username);
                sprintf(ack.buffer, "%d\n", port);
                sendMessage(ack, usd);  //invio dati al peer

                gruppo = fopen(GRUPPO, "a+");
                fprintf(gruppo, "%s\n", currentChatUname);     //memorizzazione dati peer
                fclose(gruppo);

                insert(currentChatUname, usd, dport);
                FD_SET(usd, &masterR);
                if(usd > maxfd) maxfd = usd;
                chatConnected = 1;
                grpCount++;

            }
        }
    }

    else {      //i peer sono sulla stessa chat, invio messaggi diretto

        m.type = 6;
        if(grpCount == 1) strcpy(m.receiver, currentChatUname);

        gruppo = fopen(GRUPPO, "a+");
        while(getline(&line, &len, gruppo) != -1) {   //invio messaggio a tutti i partecipanti
            sscanf(line, "%s\n", peerName);
            if(strcmp(peerName, "server") == 0) {
                if(sdserver == -1) {
                    printf("Server offline, impossibile inviare il messaggio\n");
                    fclose(gruppo);
                    return;
                }
                sendMessage(m, sdserver);
            }
            else {
                usd = search(peerName, &dport);
                sendMessage(m, usd);
            }
        }
        fclose(gruppo);
    }

}

int login() {
    char choice[7], arg1[MAXLEN+1], arg2[MAXLEN+1], arg3[MAXLEN+1], cmd[CMDMAXLEN];
    int ret, valid;
    if(!online) ret = connectToServer(sdserver, &server, "10.0.2.15", 4242);

    if(ret == -1) exit(0);
    else if(ret == 0) online = 1;

    while(1) {
        read(STDIN_FILENO, cmd, CMDMAXLEN);
        sscanf(cmd, "%6s %30s %30s %30s\n", choice, arg1, arg2, arg3);
        valid = 0;

        if(strcmp(choice, "signup") == 0) {
            strcpy(username, arg1);
            authenticate(0, arg1, arg2);
            valid = 1;     //e' stato digitato un comando valido, aspetto la risposta dal server
        }

        else if(strcmp(choice, "in") == 0) {
            strcpy(username, arg2);
            authenticate(1, arg2, arg3);
            valid = 1;
        }

        if(valid == 1) {
            message m;
            int ret = receiveMessage(&m, sdserver); 

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
                if(strcmp(m.buffer, "1\n") == 0) return 0;
                else return -1;
            }
        }

        else printf("Comando non valido...\n");
    }

}

int main(int argc, char** argv) {

    signal(SIGTERM, handler);

    int ret, i, sdclient;
    fd_set readers;
    sockaddr_in addr, client;
    message m;
    socklen_t len;
    struct timeval timeout;
    char choice[7], arg1[MAXLEN], arg2[MAXLEN], arg3[MAXLEN], buffer[MAXMESBUFLEN];

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    FD_SET(STDIN_FILENO, &masterR);

    system("clear");
    if(argc > 2) {
        printf("Numero di parametri eccessivo, .dev accetta al piu' un parametro\n");
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

    if(socketSetup(&listener, port, SOCK_STREAM | SOCK_NONBLOCK, &addr) == -1) return -1;  //creazione socket di ascolto
    listen(listener, 10);
    if(socketSetup(&sdserver, -1, SOCK_STREAM, &server) == -1) return -1;  //creazione socket di comunicazione con server

    do {
        ret = login();
        if(ret == -1) {
            system("clear");
            printf("Username o password sbagliati...\n");
        }
    } while(ret == -1);

    strcpy(GRUPPO, username);
    strcpy(GRUPPOCPY, username);
    strcpy(CHATCPY, username);
    strcat(GRUPPO, "Gruppo.txt");
    strcat(GRUPPOCPY, "GruppoCPY.txt");
    strcat(CHATCPY, "CPY.txt");

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
                    len = sizeof(client);
                    sdclient = accept(listener, (struct sockaddr*) &client, &len);
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

                }

                else if(i == STDIN_FILENO) {
                    memset(buffer, 0, MAXMESBUFLEN);
                    read(STDIN_FILENO, buffer, MAXMESBUFLEN);

                    if(chatOpened) {    //una chat e' aperta, invio il messaggio

                        if(strcmp(buffer, "\\q\n") == 0) {
                            closeChat();
                            continue;
                        }

                        else if(strcmp(buffer, "\\u\n") == 0) { //richiesta al server degli utenti online
                            addUser();
                        }

                        else {
                            Chat(buffer);
                            readChat();
                        }
                    }

                    else {

                        sscanf(buffer, "%6s %30s %30s %30s\n", choice, arg1, arg2, arg3);
                    
                        if(strcmp(choice, "chat") == 0) {
                            
                            strcpy(currentChatUname, arg1);
                            strcpy(currentChat, username);
                            strcat(currentChat, "_");
                            strcat(currentChat, arg1);
                            strcat(currentChat, ".txt");   //trovo il file relativo al nome utente inserito o ne creo uno nuovo
                            chat = fopen(currentChat, "a+");
                            gruppo = fopen(GRUPPO, "a+");
                            fclose(chat);
                            fclose(gruppo);

                            readChat();

                            chatOpened = 1;
                            chatConnected = 0;

                        }

                        else if(strcmp(choice, "out") == 0) {
                            system("clear");
                            if(online) {
                                close(sdserver);
                                sdserver = -1;
                                online = 0;
                            }
                            close(listener);
                            flushList();
                            return 0;
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
                        printf("Server offline\n");
                    }

                    if(m.type == 2 && strcmp(m.buffer, "1\n") == 0) {   //messaggio letto
                        updateChat(m.timestamp);
                    }

                    else if(m.type == 4) {   //ricevuto un messaggio di avvio chat

                        char file[MAXLEN+4];
                        strcpy(file, username);
                        strcat(file, "_");
                        strcat(file, m.sender);
                        strcat(file, ".txt");
                        FILE *tmp = fopen(file, "a+");
                        fprintf(tmp, "%d >%s", m.timestamp, m.buffer);
                        fclose(tmp);

                        if(strcmp(file, currentChat) == 0) {    //peer nella stessa chat
                            readChat();
                        }

                    }

                }

                else {

                    ret = receiveMessage(&m, i);

                    if(grpCount == 1 && chatConnected) {    //un solo partecipante, in attesa di ACK
                        if(ret == 0 || ret == -1) {
                            peer *p = extract(i);
                            if(p != NULL) free(p);
                            grpCount = chatConnected = 0;
                            close(i);
                            FD_CLR(i, &masterR);
                            continue;
                        }

                        else if(m.type == 2 && strcmp(m.buffer, "1\n") == 0) {   //messaggio letto
                            
                            updateChat(m.timestamp);
                        }
                    }

                    if(m.type == 6) {   //ricevuto un messaggio da un peer
                            
                            char file[MAXLEN+4];
                            strcpy(file, username);
                            strcat(file, "_");
                            strcat(file, m.sender);
                            strcat(file, ".txt");
                            chat = fopen(file, "a+");

                        if(grpCount <= 1) {
                            fprintf(chat, "%d >%s", m.timestamp, m.buffer);
                            fclose(chat);

                            if(strcmp(file, currentChat) == 0) {    //peer nella stessa chat
                                readChat();
                            }
                        }
                        else {
                            chat = fopen(currentChat, "a+");
                            fprintf(chat, "%d %s", m.timestamp, m.buffer);
                            fclose(chat);
                        }

                    }

                }

            }
        }
    }
}