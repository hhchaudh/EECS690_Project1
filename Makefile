Main: main.o
	g++ -std=c++11 -g -Wall main.o -o main -lpthread

main.o: main.cpp barrier.h
	g++ -std=c++11 -g -Wall -c main.cpp

clean:
	rm *.o main
