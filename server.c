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

#define MAXLEN 30
#define CMDMAXLEN 11+2*MAXLEN
#define MAXMESBUFLEN 1024
#define DBUTENTI "dbutenti.txt"
#define REGISTRO "registro.txt"
#define HANGING "hanging.txt"
#define CPY "cpy.txt"

typedef struct sockaddr_in sockaddr_in;
int listener;
sockaddr_in addr, client;
fd_set masterR;

struct message {

    uint8_t type;
    uint32_t timestamp;
    char sender[MAXLEN], receiver[MAXLEN];
    char buffer[MAXMESBUFLEN];

} typedef message;

struct toInsert {

    int sd, port;
    struct toInsert *next;

} typedef toInsert;
toInsert *insertList = NULL;

void insert(int sd, int port) {
    toInsert *tmp = (toInsert*) malloc(sizeof(toInsert));
    tmp->sd = sd;
    tmp->port = port;

    if(insertList == NULL) {
        tmp->next = NULL;
        insertList = tmp;
        return;
    }

    tmp->next = insertList;
    insertList = tmp;
}

toInsert* extract(int sd) {

    if(insertList == NULL) return NULL;

    toInsert *p = insertList, *q;

    if(insertList->sd == sd) {
        insertList = insertList->next;
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

void setOnline(int sd, char *uname, int port) {

    time_t sec = time(NULL);
    FILE *online = fopen(REGISTRO, "a+");

    fprintf(online, "%s %d %ld %ld %d\n", uname, port, sec, (long) 0, sd);
    fclose(online);

}

void setOffline(int sd) {
    
    FILE *online = fopen(REGISTRO, "r");
    FILE *tmp = fopen(CPY, "a+");
    if(online == NULL || tmp == NULL) return;
    char uname[MAXLEN], *line = NULL;
    int sdr, port;
    long login, logout;
    size_t len = 0;

    while(getline(&line, &len, online) != -1) {
        
        sscanf(line, "%s %d %ld %ld %d\n", uname, &port, &login, &logout, &sdr);
        if(sd == sdr) {
            time_t sec = time(NULL);
            fprintf(tmp, "%s %d %ld %ld %d\n", uname, port, login, sec, sdr);
        }
        else fprintf(tmp, "%s", line);
        
    }

    fclose(online);
    fclose(tmp);
    remove(REGISTRO);
    rename(CPY, REGISTRO);

}

int isOnline(char *user, int *prt) {

    FILE *online = fopen(REGISTRO, "r");
    if(online == NULL) return -1;
    char uname[MAXLEN+1], *line = NULL;
    size_t len = 0;
    int sdr, port;
    long login, logout;

    while(getline(&line, &len, online) != -1) {
        sscanf(line, "%s %d %ld %ld %d\n", uname, &port, &login, &logout, &sdr);
        if(strcmp(user, uname) == 0 && logout == 0) {
            fclose(online);
            *prt = port;
            return sdr;
        }
    }

    fclose(online);
    *prt = -1;
    return -1;

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
    if(ret == 0) {  //il client e' andato offline, aggiornamento timestamp
        setOffline(sd);
        FD_CLR(sd, &masterR);
        close(sd);
        return 0;
    }

    else if(ret == -1) return ret;

    ret = recv(sd, (void*) &(*m).timestamp, sizeof(uint32_t), MSG_WAITALL);
    if(ret == 0) {
        setOffline(sd);
        FD_CLR(sd, &masterR);
        close(sd);
        return 0;
    }

    else if(ret == -1) return ret;

    (*m).timestamp = ntohl((*m).timestamp);

    ret = recv(sd, (void*) (*m).sender, MAXLEN, MSG_WAITALL);
    if(ret == 0) {
        setOffline(sd);
        FD_CLR(sd, &masterR);
        close(sd);
        return 0;
    }
                    
    else if(ret == -1) return ret;

    ret = recv(sd, (void*) (*m).receiver, MAXLEN, MSG_WAITALL);
    if(ret == 0) {
        setOffline(sd);
        FD_CLR(sd, &masterR);
        close(sd);
        return 0;
    }
                        
    else if(ret == -1) return ret;

    ret = recv(sd, (void*) (*m).buffer, MAXMESBUFLEN, MSG_WAITALL);
    if(ret == 0) {
        setOffline(sd);
        FD_CLR(sd, &masterR);
        close(sd);
        return 0;
    }
                    
    else if(ret == -1) return ret;

    return 1;

}

void cmdlist() {
    system("clear");
    printf("******************** SERVER STARTED ********************\n");
    printf("1)\thelp -> mostra i dettagli dei comandi\n");
    printf("2)\tlist -> mostra l'elenco degli utenti connessi\n");
    printf("3)\tesc -> chiude il server\n");
}

void cleantstdin() {
    char c;
    do {
        c = getchar();
    } while (c != '\n' && c != EOF);
}

int socketSetup(int port) {      //inizializza il socket di comunicazione con il server e si connette ad esso

    int ret;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "10.0.2.15", &addr.sin_addr);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener == -1) {
        perror("Impossibile creare un socket di comunicazione, errore: ");
        return -1;
    }

    ret = bind(listener, (struct sockaddr*) &addr, sizeof(addr));
    if(ret == -1) {
        perror("Utilizzare un'altra porta, errore: ");
        close(listener);
        return -1;
    }

    listen(listener, 10);
    return 0;

}

void list() {

    FILE *online = fopen(REGISTRO, "a+");
    char *line = NULL, uname[MAXLEN];
    int sd, port;
    long login, logout;
    struct tm tin;
    size_t len = 0;

    while(getline(&line, &len, online) != -1) {
        sscanf(line, "%s %d %ld %ld %d\n", uname, &port, &login, &logout, &sd);
        if(logout == 0) {
            memcpy(&tin, localtime(&login), sizeof(struct tm));   //conversione secondi da epoch a formato leggibile
            printf("%s %d %d:%d:%d\n", uname, port, tin.tm_hour, tin.tm_min, tin.tm_sec);
        }
    }

    fclose(online);
    
}

int checkUsername(char *arg1) {
    FILE *utenti = fopen(DBUTENTI, "r");
    char *line = NULL;
    char uname[MAXLEN];
    size_t len = 0;

    while(getline(&line, &len, utenti) != -1) {
        sscanf(line, "%s", uname);
        if((strcmp(uname, arg1) == 0)) return -1;
    }

    fclose(utenti);
    return 0;
}

void sendHanging(int type, char *uname) {

    int usd, dport;
    char *line = NULL;
    size_t len = 0;
    message m;
    FILE *tmp = fopen(HANGING, "a+");
    FILE *cpy = fopen(CPY, "a+");

    while(getline(&line, &len, tmp) != -1) {
        sscanf(line, "%d %d %s %s %99[^\n]", &m.type, &m.timestamp, m.sender, m.receiver, m.buffer);
        if(strcmp(uname, m.receiver) == 0 && m.type == type) {
            usd = isOnline(uname, &dport);
            sendMessage(m, usd);
        }
        else fprintf(cpy, "%s", line);
    }

    fclose(tmp);
    fclose(cpy);
    remove(HANGING);
    rename(CPY, HANGING);

}

void signup(int sd, char *arg1, char *arg2, int port) {
    message ack;
    ack.type = 2;
    ack.timestamp = 0;

    if(checkUsername(arg1) == -1) {
        strcpy(ack.buffer, "0\n");
        sendMessage(ack, sd);
        return;
    }

    FILE *utenti = fopen(DBUTENTI, "a+");
    fprintf(utenti, "%s %s\n", arg1, arg2);
    fclose(utenti);
    setOnline(sd, arg1, port);
    
    strcpy(ack.buffer, "1\n");
    sendMessage(ack, sd);
}

int in(int sd, char *arg1, char *arg2, int port) {
    FILE *utenti = fopen(DBUTENTI, "r");
    char *line = NULL;
    char uname[MAXLEN], pwd[MAXLEN];
    size_t len = 0;

    while(getline(&line, &len, utenti) != -1) {
        sscanf(line, "%s %s", uname, pwd);
        if((strcmp(uname, arg1) == 0) && (strcmp(pwd, arg2) == 0)) {
            fclose(utenti);
            setOnline(sd, arg1, port);
            return 0;
        }
    }

    fclose(utenti);
    return -1;
}

int main(int argc, char** argv) {

    int port = 4242, sdclient, ret, maxfd = STDIN_FILENO, i = 0;
    char cmd[CMDMAXLEN], choice[MAXLEN];
    socklen_t len;
    struct timeval timeout;
    fd_set readers;
    FILE *utenti, *online, *toSend;

    //creazione del database e registro di utenti
    utenti = fopen(DBUTENTI, "a+");
    fclose(utenti);
    online = fopen(REGISTRO, "a+");
    fclose(online);
    toSend = fopen(HANGING, "a+");
    fclose(toSend);


    FD_SET(STDIN_FILENO, &masterR);
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    system("clear");
    if(argc > 2) {
        printf("Numero di parametri eccessivo, .dev accetta al piu' un parametro\n");
        return -1;
    }
    else if(argc == 2) port = atoi(argv[1]);
    
    if(port < 1024 || port > 65535) {
        printf("La porta selezionata non e' valida\n");
        return -1;
    }

    ret = socketSetup(port);
    if(ret != -1) printf("SERVER AVVIATO!\n");
    else return ret;

    if(listener > maxfd) maxfd = listener;
    FD_SET(listener, &masterR);

    cmdlist();
    
    while(1) {

        readers = masterR;
        select(maxfd + 1, &readers, NULL, NULL, &timeout);

        for(i = 0; i < maxfd + 1; i++) {
            if(FD_ISSET(i, &readers)) {

                if(i == listener) {     //ricevuta una nuova richiesta
                    len = sizeof(client);
                    memset(&client, 0, len);
                    sdclient = accept(listener, (struct sockaddr*) &client, &len);
                    FD_SET(sdclient, &masterR);
                    if(sdclient > maxfd) maxfd = sdclient;
                    insert(sdclient, (int) client.sin_port);
                }

                else if(i == STDIN_FILENO) {        //ricevuto un input da terminale
                    read(STDIN_FILENO, cmd, CMDMAXLEN);
                    sscanf(cmd, "%s\n", choice);
                    if(strcmp(choice, "help") == 0) {
                        system("clear");
                        printf("-list: mostra l'elenco degli utenti connessi indicando username, timestamp di connessione e porta\n");
                        printf("-esc: chiude il server, non impedisce la continuazione di una chat gia' avviata ma non permette agli utenti di effettuare login\n");
                    }

                    else if(strcmp(choice, "list") == 0) {
                        system("clear");
                        list();
                    }

                    else if(strcmp(choice, "esc") == 0) {
                        printf("Chiusura server in corso...\n");
                        int j;
                        for(j = 0; j < maxfd + 1; j++) {
                            if(FD_ISSET(j, &masterR)) close(j);
                        }

                        return 0;
                    }

                    else {
                        printf("Comando non valido...\n");
                    }

                    printf("Premere invio per continuare...\n");
                    cleantstdin();
                    while(getchar() != '\n');
                    cmdlist();

                }

                else {      //socket di comunicazione con un client pronto

                    message m;

                    ret = receiveMessage(&m, i);
                    
                    if(ret == -1 || ret == 0) continue;

                    char arg1[MAXLEN], arg2[MAXLEN];
                    int cport;

                    if(m.type == 0) {
                        sscanf(m.buffer, "%s %s %d\n", arg1, arg2, &cport);
                        signup(i, arg1, arg2, cport);
                    }

                    else if(m.type == 1) {
                        sscanf(m.buffer, "%s %s %d\n", arg1, arg2, &cport);
                        message ack;
                        ack.timestamp = 0;
                        ack.type = 2;

                        if(in(i, arg1, arg2, cport) == -1){
                            strcpy(ack.buffer, "0\n");
                            sendMessage(ack, i);
                        }
                        else {
                            strcpy(ack.buffer, "1\n");
                            sendMessage(ack, i);
                            sendHanging(2, arg1);
                            sendHanging(4, arg1);
                        }

                    }

                    else if(m.type == 2) {  //ricevuto ack

                        int dport;
                        sdclient = isOnline(m.receiver, &dport);
                        if(sdclient != -1) sendMessage(m, sdclient);
                        else {
                            toSend = fopen(HANGING, "a+");
                            fprintf(toSend, "%d %d %s %s %s", m.type, m.timestamp, m.sender, m.receiver, m.buffer);
                            fclose(toSend);
                        }

                    }

                    else if(m.type == 4) {  //ricevuto un messaggio per iniziare una chat

                        int dport;
                        sdclient = isOnline(m.receiver, &dport);
                        if (sdclient != -1) {   //peer online, recapito messaggio
                            sendMessage(m, sdclient);
                        }

                        else {  //peer offline, bufferizzazione
                            toSend = fopen(HANGING, "a+");
                            fprintf(toSend, "%d %d %s %s %s", m.type, m.timestamp, m.sender, m.receiver, m.buffer);
                            fclose(toSend);
                        }

                        message ack;
                        ack.type = 5;
                        if(sdclient != -1) sprintf(ack.buffer, "%d\n", dport);
                        else sprintf(ack.buffer, "%d\n", -1);
                        sendMessage(ack, i);    //invio dati peer

                    }

                }

            }
        }

    }

}