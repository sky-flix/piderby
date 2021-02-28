cmake_minimum_required(VERSION 3.19)

include(pico_sdk_import.cmake)

project(piderby)

pico_sdk_init()

add_executable(piderby
        piderby.c
        )

# Pull in our pico_stdlib which pulls in commonly used features
target_link_libraries(piderby pico_stdlib hardware_i2c)

# create map/bin/hex file etc.
pico_add_extra_outputs(piderby)
