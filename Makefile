#Set inclusion paths here (if boost, bamtools, or args are installed outside your path)
INCLUDE_DIRS=-I/usr/local/include/bamtools
#Set library paths here (if boost or bamtools are installed outside your path)
LIBRARY_PATHS=
#Set to 0 if you encounter linker errors regarding strings from the bamtools library
ABI=1
#Provide full paths here to .a archives for libraries which should be statically linked
STATIC_LIBS=
#List of remaining libraries that will be dynamically linked
LIBS=-lbamtools -lboost_filesystem -lboost_regex -lboost_system -lz

CC=g++
STDLIB=-std=c++14
CFLAGS=-Wall $(STDLIB) -D_GLIBCXX_USE_CXX11_ABI=$(ABI) -O3
SOURCES=BED.cpp Expression.cpp GTF.cpp RNASeQC.cpp Metrics.cpp
SRCDIR=src
OBJECTS=$(SOURCES:.cpp=.o)

rnaseqc: $(foreach file,$(OBJECTS),$(SRCDIR)/$(file))
	$(CC) -O3 $(LIBRARY_PATHS) -o $@ $^ $(STATIC_LIBS) $(LIBS)

.PRECIOUS: src/%.cpp

src/%.cpp: IntervalTree/%.cpp
	mkdir -p src
	cp $(wildcard IntervalTree/*.cpp) $(wildcard IntervalTree/*.h) src

%.o: %.cpp
	$(CC) $(CFLAGS) -I. $(INCLUDE_DIRS) -c -o $@ $<

.PHONY: clean

clean:
	rm $(wildcard $(SRCDIR)/*.o)
