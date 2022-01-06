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
#define CMDMAXLEN 11+2*MAXLEN
#define MAXMESBUFLEN 1024
#define DBUTENTI "dbutenti.txt"
#define REGISTRO "registro.txt"
#define HANGINGHDR "hanginghdr.txt"
#define HANGING "hanging.txt"
#define CPY "cpy.txt"

//variabili di appoggio

typedef struct sockaddr_in sockaddr_in;
int listener, dport, ret;
long login, logout;
size_t len = 0;
char *line = NULL, uname[MAXLEN];
time_t sec;
sockaddr_in addr, client;
fd_set masterR;
FILE *utenti, *online, *toSend, *cpy;

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

void insert(int sd, int port) {
    peer *tmp = (peer*) malloc(sizeof(peer));
    tmp->sd = sd;
    tmp->port = port;

    if(peerList == NULL) {
        tmp->next = NULL;
        peerList = tmp;
        return;
    }

    tmp->next = peerList;
    peerList = tmp;
}

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

/*Modifica il registro ed inserisce una entry*/
void setOnline(int sd, char *uname, int port) {

    time_t sec = time(NULL);
    FILE *online = fopen(REGISTRO, "a+");

    fprintf(online, "%s %d %ld %ld %d\n", uname, port, sec, (long) 0, sd);
    fclose(online);

    //associa al socket descriptor username e porta del client
    peer *p = searchBySD(sd);
    if(p != NULL) {
        strcpy(p->uname, uname);
        p->port = port;
    }

}

/*Rimuove la entry dal registro con socket descriptor uguale a sd e timestamp di logout nullo*/
void setOffline(int sd) {
    
    online = fopen(REGISTRO, "a+");
    cpy = fopen(CPY, "a+");
    int sdr, port;

    while(getline(&line, &len, online) != -1) {
        
        sscanf(line, "%s %d %ld %ld %d\n", uname, &port, &login, &logout, &sdr);
        if(sd == sdr && logout == 0) {
            sec = time(NULL);
            fprintf(cpy, "%s %d %ld %ld %d\n", uname, port, login, sec, sdr);
        }
        else fprintf(cpy, "%s", line);
        
    }

    fclose(online);
    fclose(cpy);
    remove(REGISTRO);
    rename(CPY, REGISTRO);

}

/*Restituisce il sd di un utente se online altrimenti -1*/
int isOnline(char *user, int *prt) {

    return search(user, prt);

}

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

int receiveMessage(message *m, int sd) {

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

    uint16_t lmsg;
    int length;

    ret = recv(sd, (void*)&lmsg, sizeof(uint16_t), 0);
    if(ret == 0) {
        setOffline(sd);
        FD_CLR(sd, &masterR);
        close(sd);
        return 0;
    }
                    
    else if(ret == -1) return ret;
    length = ntohs(lmsg);

    ret = recv(sd, (void*) (*m).buffer, length, MSG_WAITALL);
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

/*inizializza il socket di comunicazione con il server e si connette ad esso*/
int socketSetup(int port) {

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if(listener == -1) {
        perror("Impossibile creare un socket di comunicazione, errore");
        return -1;
    }

    ret = bind(listener, (struct sockaddr*) &addr, sizeof(addr));
    if(ret == -1) {
        perror("Utilizzare un'altra porta, errore");
        close(listener);
        return -1;
    }

    listen(listener, 10);
    return 0;

}

/*Gestisce il comando list*/
void list() {

    FILE *online = fopen(REGISTRO, "a+");
    line = NULL;
    len = 0;
    int sd, port;
    struct tm tin;

    while(getline(&line, &len, online) != -1) {
        sscanf(line, "%s %d %ld %ld %d\n", uname, &port, &login, &logout, &sd);
        if(logout == 0) {
            memcpy(&tin, localtime(&login), sizeof(struct tm));   //conversione secondi da epoch a formato leggibile
            printf("%s %d %d:%d:%d\n", uname, port, tin.tm_hour, tin.tm_min, tin.tm_sec);
        }
    }

    fclose(online);
    
}

/*Restituisce 0 se lo username arg1 non è presente nel db altrimenti -1*/
int checkUsername(char *arg1) {
    utenti = fopen(DBUTENTI, "r");
    line = NULL;
    len = 0;

    while(getline(&line, &len, utenti) != -1) {
        sscanf(line, "%s", uname);
        if((strcmp(uname, arg1) == 0)) return -1;
    }

    fclose(utenti);
    return 0;
}

/*Gestisce un comando di show da parte di un client*/
void sendHanging(int type, char *sender, char *receiver) {

    int usd;
    line = NULL;
    len = 0;
    message m;
    toSend = fopen(HANGING, "a+");
    cpy = fopen(CPY, "a+");

    while(getline(&line, &len, toSend) != -1) {
        sscanf(line, "%hhd %d %s %s %99[^\n]", &m.type, &m.timestamp, m.sender, m.receiver, m.buffer);
        if(strcmp(sender, m.receiver) == 0 && m.type == type            //controlla se il ricevitore e' quello giusto e nel caso che sia specificato anche il trasmettitore
            && (receiver == NULL || (receiver != NULL && strcmp(receiver, m.sender) == 0))) {
            strcat(m.buffer, "\n");
            usd = isOnline(sender, &dport);
            sendMessage(m, usd);
        }
        else fprintf(cpy, "%s", line);
    }

    fclose(toSend);
    fclose(cpy);
    remove(HANGING);
    rename(CPY, HANGING);

}

/*Registra un utente*/
void signup(int sd, char *arg1, char *arg2, int port) {
    message ack;
    ack.type = 2;
    ack.timestamp = 0;

    if(checkUsername(arg1) == -1) {
        strcpy(ack.buffer, "0\n");
        sendMessage(ack, sd);
        return;
    }

    utenti = fopen(DBUTENTI, "a+");
    fprintf(utenti, "%s %s\n", arg1, arg2);
    fclose(utenti);
    setOnline(sd, arg1, port);
    
    strcpy(ack.buffer, "1\n");
    sendMessage(ack, sd);
}

/*Effettua il login di un utente controllando che i dati siano giusti*/
int in(int sd, char *arg1, char *arg2, int port) {
    utenti = fopen(DBUTENTI, "r");
    line = NULL;
    len = 0;
    char pwd[MAXLEN];

    if(search(arg1, &dport) != -1) return -1;   //controlla se l'utente e' gia' online

    //altrimenti controlla credenziali
    while(getline(&line, &len, utenti) != -1) {
        sscanf(line, "%s %s\n", uname, pwd);
        if((strcmp(uname, arg1) == 0) && (strcmp(pwd, arg2) == 0)) {
            fclose(utenti);
            setOnline(sd, arg1, port);
            printf("L'utente %s si e' appena connesso\n", arg1);
            return 0;
        }
    }

    fclose(utenti);
    return -1;
}

/*Memorizza un messaggio da inviare ad un client attualmente offline*/
void memorize(message m) {

    toSend = fopen(HANGING, "a+");
    fprintf(toSend, "%hhd %d %s %s %s", m.type, m.timestamp, m.sender, m.receiver, m.buffer);
    fclose(toSend);

    if(m.type == 4 || m.type == 6) {
        toSend = fopen(HANGINGHDR, "a+");   //il file HANGINGHDR contiene per ogni combinazione di sender e receiver il numero totale di messaggi e il timestamp del più recente
        cpy = fopen(CPY, "a+");
        int count = 0, ts;
        char sender[MAXLEN], receiver[MAXLEN];

        while(getline(&line, &len, toSend) != -1) {
            sscanf(line, "%s %s %d %u\n", sender, receiver, &count, &ts);
            if(strcmp(m.sender, sender) == 0 && strcmp(m.receiver, receiver) == 0) {
                count++;
                fprintf(cpy, "%s %s %d %u\n", sender, receiver, count, ts);
                count = -1;
            }
            else fprintf(cpy, "%s", line);
        }

        if(count != -1) {       //non esiste ancora un record
            fprintf(cpy, "%s %s %d %d\n", m.sender, m.receiver, 1, m.timestamp);
        }

        fclose(toSend);
        fclose(cpy);
        remove(HANGINGHDR);
        rename(CPY, HANGINGHDR);

    }

}

void sendFile(char *name, char *uname) {

    message m;
    int usd, dport;

    if(strlen(name) > MAXMESBUFLEN) {
        printf("Nome file troppo lungo\n");
        return;
    }
    FILE *ptr = fopen(name, "rb");
    if(ptr == NULL) {
        printf("Il file specificato non esiste\n");
        return;
    }

    m.type = 8;
    strcpy(m.buffer, name);

    usd = search(uname, &dport);
    if(usd == -1) {
        printf("Impossibile condividere il file con l'utente specificato\n");
        fclose(ptr);
        return;
    }

    strcpy(m.sender, "\n");
    sendMessage(m, usd);    //invio messaggio per avviare lo sharing

    size_t ret_t = 1;
    while(1) {  //legge ed invia al piu' MAXMESBUFLEN byte
        memset(m.buffer, 0, MAXMESBUFLEN);     //pulizia
        ret_t = fread(m.buffer, sizeof(*(m.buffer)), sizeof(m.buffer)/sizeof(*(m.buffer)), ptr);
        if(ret_t == 0 || ret_t == EOF) break;     //EOF
        m.timestamp = ret_t;       //popola con il numero di byte letti
        sendMessage(m, usd);
    }
    fclose(ptr);

    //invio messaggio fine sharing
    m.type = 2;
    memset(m.buffer, 0, MAXMESBUFLEN);
    sendMessage(m, usd);

}

int main(int argc, char** argv) {

    int port = 4242, sdclient, maxfd = STDIN_FILENO, i = 0;
    char cmd[CMDMAXLEN];
    socklen_t socklen;
    struct timeval timeout;
    fd_set readers;

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
        printf("Numero di parametri eccessivo, inserire al piu' un parametro\n");
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
                    socklen = sizeof(client);
                    memset(&client, 0, socklen);
                    sdclient = accept(listener, (struct sockaddr*) &client, &socklen);
                    FD_SET(sdclient, &masterR);
                    if(sdclient > maxfd) maxfd = sdclient;
                    insert(sdclient, -1);
                    printf("Una connessione con un client e' stata creata\n");
                }

                else if(i == STDIN_FILENO) {        //ricevuto un input da terminale
                    memset(cmd, 0, CMDMAXLEN);
                    read(STDIN_FILENO, cmd, CMDMAXLEN);

                    if(strcmp(cmd, "help\n") == 0) {
                            system("clear");
                            printf("-list: mostra l'elenco degli utenti connessi indicando username, timestamp di connessione e porta\n");
                            printf("-esc: chiude il server, non impedisce la continuazione di una chat gia' avviata ma non permette agli utenti di effettuare login\n");
                    }

                    else if(strcmp(cmd, "list\n") == 0) {
                        system("clear");
                        list();
                    }
                    
                    else if(strcmp(cmd, "esc\n") == 0) {
                        printf("Chiusura server in corso...\n");
                        flushList();
                        close(listener);
                        return 0;
                    }

                    else cmdlist();

                }

                else {      //socket di comunicazione con un client pronto

                    message m;
                    ret = receiveMessage(&m, i);
                    
                    if(ret == -1) continue;
                    else if(ret == 0) {
                        peer *p = extract(i);
                        printf("L'utente %s si e' disconnesso\n", p->uname);
                        setOffline(i);
                        free(p);
                        close(i);
                        FD_CLR(i, &masterR);
                        continue;
                    }

                    char arg1[MAXLEN], arg2[MAXLEN];
                    int cport;

                    if(m.type == 0) {
                        sscanf(m.buffer, "%s %s %d\n", arg1, arg2, &cport);
                        signup(i, arg1, arg2, cport);
                        printf("L'utente %s si e' appenna registrato\n", m.sender);
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
                            sendHanging(2, arg1, NULL);
                        }

                    }

                    else if(m.type == 2) {  //ricevuto ack
                        printf("Ricevuto un messaggio avvenuta consegna da %s per %s\n", m.sender, m.receiver);
                        int dport;
                        sdclient = isOnline(m.receiver, &dport);
                        if(sdclient != -1) sendMessage(m, sdclient);
                        else {
                            toSend = fopen(HANGING, "a+");
                            fprintf(toSend, "%d %d %s %s %s", m.type, m.timestamp, m.sender, m.receiver, m.buffer);
                            fclose(toSend);
                        }

                    }

                    else if(m.type == 3) {  //ricevuto un timestamp di logout
                        printf("Ricevuto un messaggio con timestamp di logout da %s\n", m.sender);
                        online = fopen(REGISTRO, "a+");
                        cpy = fopen(CPY, "a+");
                        int sd, done = 0;   //variabile done per uscire al primo timestamp cambiato
                        
                        while(getline(&line, &len, online) != -1) {
                            sscanf(line, "%s %d %ld %ld %d\n", uname, &dport, &login, &logout, &sd);
                            if(strcmp(m.sender, uname) == 0 && logout == 0 && !done) {
                                logout = m.timestamp;
                                fprintf(cpy, "%s %d %ld %ld %d\n", uname, dport, login, logout, sd);
                                done = 1;
                            }
                            else fprintf(cpy, "%s %d %ld %ld %d\n", uname, dport, login, logout, sd);
                        }

                        fclose(online);
                        fclose(cpy);
                        remove(REGISTRO);
                        rename(CPY, REGISTRO);                        

                    }

                    else if(m.type == 4) {  //ricevuto un messaggio per iniziare una chat
                        printf("Ricevuto un messaggio di avvio chat da %s per %s", m.sender, m.receiver);
                        sdclient = isOnline(m.receiver, &dport);
                        if (sdclient != -1) {   //peer online, recapito messaggio
                            printf(", inoltro in corso...\n");
                            sendMessage(m, sdclient);
                        }

                        else {  //peer offline, bufferizzazione
                            printf(". Peer offline, memorizzazione...\n");
                            memorize(m);
                        }

                        message ack;
                        ack.type = 5;
                        if(sdclient != -1) sprintf(ack.buffer, "%d\n", dport);
                        else sprintf(ack.buffer, "%d\n", -1);
                        sendMessage(ack, i);    //invio dati peer

                    }

                    else if(m.type == 5) {  //ricevuta una richiesta per ricevere la lista dei peer online
                        printf("Invio lista dei peer online a %s\n", m.sender);
                        remove(CPY);
                        cpy = fopen(CPY, "a+");
                        peer *p = peerList;
                        while(p != NULL) {
                            if(p->port != -1) fprintf(cpy, "%s %d\n", p->uname, p->port);
                            p = p->next;
                        }
                        fclose(cpy);
                        sendFile(CPY, m.sender);
                        remove(CPY);
                    }

                    else if(m.type == 7) {

                        if(strcmp(m.buffer, "list\n") == 0) {
                            printf("Ricevuto un messaggio di hanging da %s\n", m.sender);
                            toSend = fopen(HANGINGHDR, "r");
                            if(toSend == NULL) {
                                m.type = 2;
                                sendMessage(m, i);
                                continue;
                            }

                            int count = 0;
                            message r;
                            r.type = 7;

                            while(getline(&line, &len, toSend) != -1) {
                                sscanf(line, "%s %s %d %u\n", r.sender, uname, &count, &r.timestamp);
                                if(strcmp(m.sender, uname) == 0) {
                                    sprintf(r.buffer, "%d", count);
                                    sendMessage(r, i);
                                }
                            }

                            r.type = 2;
                            sendMessage(r, i);
                            fclose(toSend);
                        }                        

                        else if(strcmp(m.buffer, "send\n") == 0) {
                            printf("Ricevuto un messaggio di show da %s\n", m.sender);
                            sendHanging(4, m.sender, m.receiver);
                            sendHanging(6, m.sender, m.receiver);

                            toSend = fopen(HANGINGHDR, "a+");
                            cpy = fopen(CPY, "a+");
                            char receiver[MAXLEN];
                            int count;
                            uint32_t ts;

                            while(getline(&line, &len, toSend) != -1) {
                                sscanf(line, "%s %s %d %u\n", uname, receiver, &count, &ts);
                                if(!(strcmp(m.sender, receiver) == 0 && strcmp(m.receiver, uname) == 0)) fprintf(cpy, "%s", line);
                            }

                            fclose(toSend);
                            fclose(cpy);
                            remove(HANGINGHDR);
                            rename(CPY, HANGINGHDR);

                        }

                    }

                }

            }
        }

    }

}