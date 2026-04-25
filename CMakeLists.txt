cmake_minimum_required(VERSION 3.15)
project(ya_hub)

# Устанавливаем стандарт C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Находим пакет glib-2.0 и его заголовочные файлы
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB2 REQUIRED glib-2.0)

# Ищем библиотеку MySQL C++ Connector
find_library(MYSQLCPP_LIB NAMES mysqlcppconn8 libmysqlcppconn8)
if(NOT MYSQLCPP_LIB)
    message(FATAL_ERROR "Библиотека mysqlcppconn8 не найдена. Установите mysql-connector-cpp.")
endif()

# Поиск заголовочных файлов MySQL C++ Connector в указанном пути
find_path(MYSQLCPP_INCLUDE_DIR
    NAMES
        mysqlx/xdevapi.h          # X DevAPI (современный)
        cppconn/driver.h         # Классический API
    HINTS
        /usr/include/mysql-cppconn-8
        $ENV{MYSQL_CONNECTOR_CPP_DIR}/include
    PATH_SUFFIXES
        include
    DOC "Путь к заголовочным файлам MySQL Connector/C++"
)
if(NOT MYSQLCPP_INCLUDE_DIR)
    message(FATAL_ERROR
        "Заголовочные файлы MySQL Connector/C++ не найдены в /usr/include/mysql-cppconn-8.
        Проверьте установку mysql-connector-cpp-dev.")
endif()

# Ищем библиотеки Web Toolkit (wt и wthttp)
find_library(WT_LIB NAMES wt libwt)
if(NOT WT_LIB)
    message(FATAL_ERROR "Библиотека wt не найдена. Установите wt-dev или libwt-dev.")
endif()

find_library(WTHTTP_LIB NAMES wthttp libwthttp)
if(NOT WTHTTP_LIB)
    message(FATAL_ERROR "Библиотека wthttp не найдена. Установите wt-dev или libwt-dev.")
endif()

# Ищем заголовочные файлы Web Toolkit
find_path(WT_INCLUDE_DIR
    NAMES Wt/WApplication.h
    HINTS
        /usr/include/wt
        /usr/local/include/wt
    PATH_SUFFIXES
        wt
        include/wt
    DOC "Путь к заголовочным файлам Web Toolkit"
)
if(NOT WT_INCLUDE_DIR)
    message(FATAL_ERROR "Заголовочные файлы Web Toolkit (Wt/Wt.h) не найдены. Установите wt-dev.")
endif()

# Ищем библиотеку и заголовочные файлы Mosquitto
find_library(MOSQUITTO_LIB NAMES mosquitto libmosquitto)
if(NOT MOSQUITTO_LIB)
    message(FATAL_ERROR "Библиотека mosquitto не найдена. Установите mosquitto-dev.")
endif()

find_path(MOSQUITTO_INCLUDE_DIR
    NAMES mosquitto.h
    PATH_SUFFIXES mosquitto include/mosquitto
)
if(NOT MOSQUITTO_INCLUDE_DIR)
    message(FATAL_ERROR "Заголовочные файлы mosquitto не найдены. Убедитесь, что установлен mosquitto-dev.")
endif()

# Собираем список всех .cpp файлов в текущей директории
file(GLOB CURRENT_SOURCES "*.cpp")

# Собираем список всех .cpp файлов в директории ../functions/src
file(GLOB FUNCTIONS_SOURCES "../functions/src/*.cpp")

# Объединяем все исходные файлы в один список
set(ALL_SOURCES
    ${CURRENT_SOURCES}
    ${FUNCTIONS_SOURCES}
)

# Создаём исполняемый файл из всех собранных исходных файлов
add_executable(ya_hub ${ALL_SOURCES})

# Указываем директории для поиска заголовочных файлов
target_include_directories(ya_hub PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../functions/src
    ${GLIB2_INCLUDE_DIRS}
    ${MYSQLCPP_INCLUDE_DIR}
    ${WT_INCLUDE_DIR}
    ${MOSQUITTO_INCLUDE_DIR}
)

# Добавляем общие флаги компилятора
target_compile_options(ya_hub PRIVATE
    -Wall
    -Wextra
    -O2
    -pipe
    -march=native
    -MMD
    -MP
    -Wpmf-conversions
    -fexceptions
)

# Настройки для конфигурации Debug
target_compile_definitions(ya_hub
    PRIVATE
        $<$<CONFIG:Debug>:__DEBUG__>
)
target_compile_options(ya_hub PRIVATE
    $<$<CONFIG:Debug>:-g -O0>
)

# Настройки для конфигурации Release
target_compile_definitions(ya_hub
    PRIVATE
        $<$<CONFIG:Release>:__RELEASE__>
)
target_compile_options(ya_hub PRIVATE
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# Указываем библиотеки для линковки
target_link_libraries(ya_hub PRIVATE
    pthread
    ${GLIB2_LIBRARIES}
    ${WT_LIB}
    ${WTHTTP_LIB}
    ${MYSQLCPP_LIB}
    ${MOSQUITTO_LIB}
)

#копируем служебные файлы в каталог, где будет собрана и запущена программа. 
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ya_hub.conf
     DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/build)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/sendemail.sh
     DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/build)
#file(MAKE_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build/css)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/css
     DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/build)
