all:
	g++ -std=c++11 -O3 -g -Wall -pthread -fmessage-length=0 -o nogo nogo.cpp
clean:
	rm nogo