cmake_minimum_required(VERSION 3.20)
project(CS2_Schema_Radar LANGUAGES CXX)

# Şema hiyerarşileri için C++20 standardını zorunlu kılıyoruz
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Kaynak kod ve başlık dosyalarını derleyiciye tanıtıyoruz
add_executable(CS2_Radar main.cpp offsets.hpp client_dll.hpp)

# Windows API fonksiyonları için gerekli sistem kütüphanelerini bağla
target_link_libraries(CS2_Radar PRIVATE User32 Advapi32)

