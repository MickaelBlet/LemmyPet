CXX      = x86_64-w64-mingw32-g++
WINDRES  = x86_64-w64-mingw32-windres
CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00
LDFLAGS  = -mwindows -municode -static -s
LIBS     = -lshlwapi -lgdi32 -luser32 -lshell32 -lole32 -ladvapi32 -lcomctl32 -lwtsapi32
TARGET   = LemmyPet.exe
SPRITES  = $(wildcard sprites/*.png)
OBJS     = $(patsubst src/%.cpp,build/%.o,$(wildcard src/*.cpp))
HEADERS  = $(wildcard src/*.h)

all: $(TARGET)

$(TARGET): $(OBJS) build/app.res.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

build/%.o: src/%.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/app.res.o: src/app.rc src/resource.h src/app.ico src/app.manifest | build
	$(WINDRES) -I src $< -O coff -o $@

src/app.ico: pets/lemmy/lemmy.png tools/make_icon.py
	python3 tools/make_icon.py

pets/lemmy/lemmy.png: $(SPRITES) tools/make_sheet.py
	python3 tools/make_sheet.py

build:
	mkdir -p build

clean:
	rm -rf build $(TARGET)

.PHONY: all clean
