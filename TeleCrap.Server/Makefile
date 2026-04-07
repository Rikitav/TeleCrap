CXX = g++
CXXFLAGS = -std=c++20 -Wall -O2 -DSOL_USING_CXX_LUA=1 -DSOL_ALL_SAFETIES_ON=1

INCLUDES = -I../TeleCrap.Shared/include -I../TeleCrap.Libs.SQLite/include -I../TeleCrap.Libs.Sol/include

LDFLAGS = -lpthread -ldl -lm

SRCS_CXX = $(wildcard ./src/*.cpp)
SRCS_CXX += $(wildcard ../TeleCrap.Shared/*.cpp)
SRCS_CXX += $(wildcard ../TeleCrap.Libs.SQLite/*.cpp)
SRCS_CXX += $(wildcard ../TeleCrap.Libs.Sol/*.cpp)

SRCS_C = $(wildcard ./src/*.c)
SRCS_C += $(wildcard ../TeleCrap.Shared/*.c)
SRCS_C += $(wildcard ../TeleCrap.Libs.SQLite/*.c)
SRCS_C += $(filter-out ../TeleCrap.Libs.Sol/lua.c ../TeleCrap.Libs.Sol/luac.c, $(wildcard ../TeleCrap.Libs.Sol/*.c))

OBJS_CXX = $(SRCS_CXX:.cpp=.o)
OBJS_C = $(SRCS_C:.c=.o)
OBJS = $(OBJS_CXX) $(OBJS_C)

TARGET = telecrap_server

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.c
	$(CXX) $(CXXFLAGS) -x c++ $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)