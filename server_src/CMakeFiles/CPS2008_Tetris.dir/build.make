# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.18

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Disable VCS-based implicit rules.
% : %,v


# Disable VCS-based implicit rules.
% : RCS/%


# Disable VCS-based implicit rules.
% : RCS/%,v


# Disable VCS-based implicit rules.
% : SCCS/s.%


# Disable VCS-based implicit rules.
% : s.%


.SUFFIXES: .hpux_make_needs_suffix_list


# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /home/xandru/.local/lib/python3.8/site-packages/cmake/data/bin/cmake

# The command to remove a file.
RM = /home/xandru/.local/lib/python3.8/site-packages/cmake/data/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/xandru/Documents/CPS2008_Tetris_Server/server_src

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/xandru/Documents/CPS2008_Tetris_Server/server_src

# Include any dependencies generated for this target.
include CMakeFiles/CPS2008_Tetris.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/CPS2008_Tetris.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/CPS2008_Tetris.dir/flags.make

CMakeFiles/CPS2008_Tetris.dir/server.c.o: CMakeFiles/CPS2008_Tetris.dir/flags.make
CMakeFiles/CPS2008_Tetris.dir/server.c.o: server.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/xandru/Documents/CPS2008_Tetris_Server/server_src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/CPS2008_Tetris.dir/server.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/CPS2008_Tetris.dir/server.c.o -c /home/xandru/Documents/CPS2008_Tetris_Server/server_src/server.c

CMakeFiles/CPS2008_Tetris.dir/server.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/CPS2008_Tetris.dir/server.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/xandru/Documents/CPS2008_Tetris_Server/server_src/server.c > CMakeFiles/CPS2008_Tetris.dir/server.c.i

CMakeFiles/CPS2008_Tetris.dir/server.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/CPS2008_Tetris.dir/server.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/xandru/Documents/CPS2008_Tetris_Server/server_src/server.c -o CMakeFiles/CPS2008_Tetris.dir/server.c.s

# Object files for target CPS2008_Tetris
CPS2008_Tetris_OBJECTS = \
"CMakeFiles/CPS2008_Tetris.dir/server.c.o"

# External object files for target CPS2008_Tetris
CPS2008_Tetris_EXTERNAL_OBJECTS =

CPS2008_Tetris: CMakeFiles/CPS2008_Tetris.dir/server.c.o
CPS2008_Tetris: CMakeFiles/CPS2008_Tetris.dir/build.make
CPS2008_Tetris: CMakeFiles/CPS2008_Tetris.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/xandru/Documents/CPS2008_Tetris_Server/server_src/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable CPS2008_Tetris"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/CPS2008_Tetris.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/CPS2008_Tetris.dir/build: CPS2008_Tetris

.PHONY : CMakeFiles/CPS2008_Tetris.dir/build

CMakeFiles/CPS2008_Tetris.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/CPS2008_Tetris.dir/cmake_clean.cmake
.PHONY : CMakeFiles/CPS2008_Tetris.dir/clean

CMakeFiles/CPS2008_Tetris.dir/depend:
	cd /home/xandru/Documents/CPS2008_Tetris_Server/server_src && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/xandru/Documents/CPS2008_Tetris_Server/server_src /home/xandru/Documents/CPS2008_Tetris_Server/server_src /home/xandru/Documents/CPS2008_Tetris_Server/server_src /home/xandru/Documents/CPS2008_Tetris_Server/server_src /home/xandru/Documents/CPS2008_Tetris_Server/server_src/CMakeFiles/CPS2008_Tetris.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/CPS2008_Tetris.dir/depend

