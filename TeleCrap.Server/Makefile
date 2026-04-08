CXX = g++
CC = gcc

CXXFLAGS = -std=c++20 -Wall -O2 -DSOL_USING_CXX_LUA=1 -DSOL_ALL_SAFETIES_ON=1
CFLAGS = -Wall -O2

INCLUDES = -I../TeleCrap.Shared/include -I../Telecrap.Libs.SQLite/include -I../Telecrap.Libs.Sol/include

LDFLAGS = -lpthread -ldl -lm

SRCS_CXX = $(wildcard ./src/*.cpp)
SRCS_CXX += $(wildcard ../TeleCrap.Shared/*.cpp)
SRCS_CXX += $(wildcard ../Telecrap.Libs.SQLite/src/*.cpp)
SRCS_CXX += $(wildcard ../Telecrap.Libs.Sol/src/*.cpp)

SRCS_C_SQLITE = $(wildcard ../Telecrap.Libs.SQLite/src/*.c)
SRCS_C_LUA = $(wildcard ../Telecrap.Libs.Sol/src/*.c)

OBJS = $(SRCS_CXX:.cpp=.o) $(SRCS_C_SQLITE:.c=.o) $(SRCS_C_LUA:.c=.o)

TARGET = telecrap_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

../Telecrap.Libs.Sol/src/%.o: ../Telecrap.Libs.Sol/src/%.c
	$(CXX) $(CXXFLAGS) -x c++ $(INCLUDES) -c $< -o $@

../Telecrap.Libs.SQLite/src/%.o: ../Telecrap.Libs.SQLite/src/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)