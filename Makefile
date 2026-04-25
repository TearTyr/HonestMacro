CXX = g++
IMGUI_DIR = Dependencies/imgui

IMGUI_SOURCES = $(IMGUI_DIR)/imgui.cpp \
$(IMGUI_DIR)/imgui_draw.cpp \
$(IMGUI_DIR)/imgui_tables.cpp \
$(IMGUI_DIR)/imgui_widgets.cpp \
$(IMGUI_DIR)/backends/imgui_impl_win32.cpp \
$(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
APP_SOURCES = src/main.cpp src/app.cpp \
src/HookManager.cpp src/MacroEngine.cpp src/VisionEngine.cpp \
src/miniz.c
SOURCES = $(APP_SOURCES) $(IMGUI_SOURCES)

CXXFLAGS = -O2 -flto=auto -fdata-sections -ffunction-sections \
-fno-exceptions -fno-rtti -Wall -DNDEBUG -std=gnu++17 \
-I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -Isrc \
-DMINIZ_NO_TIME

LDFLAGS = -flto=auto -Wl,--gc-sections -lopengl32 -lgdi32 -ldwmapi -lwinmm -lcomdlg32 -mwindows -s -static
TARGET = bin/HonestMacro.exe

all: $(TARGET)
$(TARGET): $(SOURCES)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"
clean:
	-rm -rf bin
.PHONY: all clean
