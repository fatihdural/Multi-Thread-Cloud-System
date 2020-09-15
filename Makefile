CFLAGS=-ansi -pedantic -errors -Wall -Wextra

All:
	gcc BibakBOXServer.c -o BibakBOXServer -lpthread -lrt
	gcc BibakBOXClient.c -o BibakBOXClient
	
