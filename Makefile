all: futexplugin futexplugin_server futexplugin_client

CXXFLAGS=-Wall -O3 -g -std=c++17

futexplugin: futexplugin.cpp futexplugin.h Makefile
	g++ ${CXXFLAGS} -o futexplugin futexplugin.cpp -lpthread

futexplugin_client: futexplugin_client.cpp futexplugin.h Makefile
	g++ ${CXXFLAGS} -o futexplugin_client futexplugin_client.cpp -lpthread

futexplugin_server: futexplugin_server.cpp futexplugin.h Makefile
	g++ ${CXXFLAGS} -o futexplugin_server futexplugin_server.cpp -lpthread

clean:
	rm -rf futexplugin futexplugin_client futexplugin_server
