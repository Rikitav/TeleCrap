CXX = g++
CC = gcc

CXXFLAGS = -std=c++20 -Wall -O2
CFLAGS = -Wall -O2

INCLUDES = -I../TeleCrap.Shared/include

LDFLAGS = -lpthread -ldl

SRCS_CXX = $(wildcard ./*.cpp)
SRCS_CXX += $(wildcard ../TeleCrap.Shared/*.cpp)

SRCS_C = $(wildcard ./*.c)
SRCS_C += $(wildcard ../TeleCrap.Shared/*.c)

OBJS_CXX = $(SRCS_CXX:.cpp=.o)
OBJS_C = $(SRCS_C:.c=.o)
OBJS = $(OBJS_CXX) $(OBJS_C)

TARGET = telecrap_tui

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)