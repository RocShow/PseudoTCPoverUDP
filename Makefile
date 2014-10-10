all:receiver.o	sender.o
receiver.o:receiver_main.c
	gcc receiver_main.c -o receiver.o
sender.o:sender_main.c
	gcc sender_main.c -o sender.o
