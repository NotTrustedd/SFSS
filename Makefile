CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -D_WIN32_IE=0x0500 -Iinclude
COMMON_LIBS = -lws2_32 -ladvapi32 -lgdi32 -luser32 -lshell32 -lcomdlg32 -lcomctl32
STATIC_FLAGS = -static -static-libgcc -static-libstdc++
APP_LDFLAGS = -mwindows $(STATIC_FLAGS) $(COMMON_LIBS)
TEST_LDFLAGS = $(STATIC_FLAGS) $(COMMON_LIBS)

BACKEND_SOURCES = \
	src/auth/User.cpp src/auth/UserManager.cpp src/auth/Authentication.cpp \
	src/security/EncryptionManager.cpp src/security/FileManager.cpp \
	src/network/Protocol.cpp src/network/Client.cpp src/network/Server.cpp \
	src/transfer/HistoryManager.cpp src/transfer/TransferManager.cpp
APP_SOURCES = src/main.cpp $(BACKEND_SOURCES)
TEST_TARGET = integration_test.exe
TARGET = SecureShare.exe

all: $(TARGET)

$(TARGET): $(APP_SOURCES)
	$(CXX) $(CXXFLAGS) $(APP_SOURCES) -o $@ $(APP_LDFLAGS)

$(TEST_TARGET): tests/integration.cpp $(BACKEND_SOURCES)
	$(CXX) $(CXXFLAGS) tests/integration.cpp $(BACKEND_SOURCES) -o $@ $(TEST_LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	if exist $(TARGET) del /Q $(TARGET)
	if exist $(TEST_TARGET) del /Q $(TEST_TARGET)

.PHONY: all test clean
