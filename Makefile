CC = gcc
# 基础编译参数
CFLAGS = -Wall -Wextra -std=c99 -D_DEFAULT_SOURCE
# 链接库参数，默认先设为空
LIBS = 

# 判断系统环境
ifeq ($(OS), Windows_NT)
	# 如果是 Windows 环境，加上网络库
	LIBS += -lws2_32
	TARGET_PING = tcpping.exe
	TARGET_PONG = tcppingd.exe
	RM = del /f /q
else
	# 如果是 Linux/Unix 环境
	LIBS += 
	TARGET_PING = tcpping
	TARGET_PONG = tcppingd
	RM = rm -f
endif

all: $(TARGET_PING) $(TARGET_PONG)

$(TARGET_PING): tcpping.c
	$(CC) $(CFLAGS) tcpping.c -o $(TARGET_PING) $(LIBS)

$(TARGET_PONG): tcppingd.c
	$(CC) $(CFLAGS) tcppingd.c -o $(TARGET_PONG) $(LIBS)

clean:
	$(RM) $(TARGET_PING) $(TARGET_PONG)

rebuild: clean all
