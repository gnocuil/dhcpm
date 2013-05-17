CC     := g++
CFLAGS := 
TARGET := dhcpm
OBJS   := main.o 

all: $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

%.o: %.cpp
	$(CC) -c $(CFLAGS) $< -o $@

clean :
	rm -f $(TARGET)
	rm -f *.o
