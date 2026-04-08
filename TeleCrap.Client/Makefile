CXX = g++
CC = gcc

CXXFLAGS = -std=c++20 -Wall -O2
CFLAGS = -Wall -O2

INCLUDES = -I../TeleCrap.Shared/include

LDFLAGS = -lpthread -ldl

SRCS_CXX = $(wildcard ./src/*.cpp)
SRCS_CXX += $(wildcard ../TeleCrap.Shared/*.cpp)

OBJS_CXX = $(SRCS_CXX:.cpp=.o)
OBJS = $(OBJS_CXX)

TARGET = telecrap_tui

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)