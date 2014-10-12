all:reliable_sender reliable_receiver
reliable_receiver:receiver_main.c
	gcc receiver_main.c -o reliable_receiver
reliable_sender:sender_main.c
	gcc sender_main.c -o reliable_sender
clean:
	rm -f reliable_sender reliable_receiver
