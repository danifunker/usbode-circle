ASM = nasm
ASMFLAGS = -f bin
TARGET = cdready.com
SOURCE = cdready.asm

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(ASM) $(ASMFLAGS) $(SOURCE) -o $(TARGET)

clean:
	DEL $(TARGET) > NUL

.PHONY: all clean
