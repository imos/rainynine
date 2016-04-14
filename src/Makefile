CXX=g++
CXXFLAGS=-O3 -g -Wall
DEFINES=-DNDEBUG -DOPTIMIZE
CPPCOMPILE=$(CXX) $(CXXFLAGS) $(DEFINES)
commands=opticalflow opticalflow-openmp radar-convert radar-diff

all: $(addprefix ~/bin/,$(commands))
clean:
	rm $(addprefix ~/bin/,$(commands))
~/bin/%-openmp: %.cpp misc.hpp
	$(CPPCOMPILE) -o $@ $(filter %.cpp,$<) -fopenmp
~/bin/%: %.cpp misc.hpp
	$(CPPCOMPILE) -o $@ $(filter %.cpp,$<)
misc.hpp: image.hpp
