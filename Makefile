OBJ = main.o
BINARY = jamail
CXXFLAGS = -O2 -Wextra -Wall `pkg-config gtk+-2.0 --cflags` -ansi -pedantic \
	-Wno-variadic-macros
LDFLAGS = `pkg-config gtk+-2.0 --libs` -lssl -lcrypto -g
CXX = g++

all: $(BINARY)

$(BINARY): $(OBJ)
	$(CXX) $(OBJ) -o $(BINARY) $(LDFLAGS)
