all: contacts dev serv

contacts: cont
	./cont
	rm cont

cont: contacts.c
	gcc contacts.c -o cont -Wall

dev: client.c
	gcc client.c -o dev -Wall

serv: server.c
	gcc server.c -o serv -Wall

clean:
	rm -f dev serv cont