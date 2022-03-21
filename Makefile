.POSIX:

baudrate: baudrate.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o baudrate baudrate.c
