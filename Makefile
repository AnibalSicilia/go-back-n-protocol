TARGET= sender receiver twister
CC= gcc
OBJECTS1= sender.o
OBJECTS2= receiver.o
OBJECTS3= twister.o

all: $(TARGET)

%.o: %.c
	$(CC) -c $^
sender: $(OBJECTS1)
	gcc -o $@ $^

receiver: $(OBJECTS2)
	gcc -o $@ $^

twister: $(OBJECTS3)
	gcc -o $@ $^

clean:
	rm -f $(TARGET) $(OBJECTS1) $(OBJECTS2) $(OBJECTS3)
	rm -f *~
