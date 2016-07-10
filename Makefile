SRC = filter gammad output ramps

OPTIMISE = -Og -g

WARN = -Wall -Wextra -pedantic -Wdouble-promotion -Wformat=2 -Winit-self -Wmissing-include-dirs \
       -Wtrampolines -Wfloat-equal -Wshadow -Wmissing-prototypes -Wmissing-declarations \
       -Wredundant-decls -Wnested-externs -Winline -Wno-variadic-macros -Wsync-nand \
       -Wunsafe-loop-optimizations -Wcast-align -Wstrict-overflow -Wdeclaration-after-statement \
       -Wundef -Wbad-function-cast -Wcast-qual -Wwrite-strings -Wlogical-op -Waggregate-return \
       -Wstrict-prototypes -Wold-style-definition -Wpacked -Wvector-operation-performance \
       -Wunsuffixed-float-constants -Wsuggest-attribute=const -Wsuggest-attribute=noreturn \
       -Wsuggest-attribute=pure -Wsuggest-attribute=format -Wnormalized=nfkc -Wconversion

FFLAGS = -fstrict-aliasing -fstrict-overflow -fipa-pure-const -ftree-vrp -fstack-usage \
         -funsafe-loop-optimizations

CCFLAGS = -std=c99 $(WARN) $(FFLAGS) $(OPTIMISE)
LDFLAGS = $(OPTIMISE) -lgamma
CPPFLAGS =


.PHONY: all
all: bin/gammad

bin/gammad: $(foreach S,$(SRC),obj/$(S).o)
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) $(LDFLAGS) -o $@ $^

obj/%.o: src/%.c src/*.h
	@mkdir -p -- "$$(dirname -- "$@")"
	$(CC) $(CCFLAGS) $(CPPFLAGS) -c -o $@ $<


.PHONY: clean
clean:
	-rm -r bin obj
