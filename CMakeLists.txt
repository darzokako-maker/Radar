cmake_minimum_required(VERSION 3.20)
project(CS2_Optimized_Radar LANGUAGES CXX)

# C++20 standardı std::min ve şemalar için zorunludur
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Kaynak kodları ve dumper başlık dosyalarını bağla
add_executable(CS2_Radar main.cpp offsets.hpp client_dll.hpp)

# Gerekli sistem kütüphanelerini bağla
target_link_libraries(CS2_Radar PRIVATE User32 Advapi32)
