cmake_minimum_required(VERSION 3.21)

project(hiqsdr-flex6xxx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Network)

qt_standard_project_setup()

qt_add_executable(hiqsdr-flex6xxx
    src/main.cpp
    src/Flex8400Emulator.cpp
    src/Flex8400Emulator.h
    src/FlexControlServer.cpp
    src/FlexControlServer.h
    src/HiqSdrBackend.cpp
    src/HiqSdrBackend.h
    src/hiqsdr/HiqSdrDevice.cpp
    src/hiqsdr/HiqSdrDevice.h
    src/RadioBackend.cpp
    src/RadioBackend.h
    src/RadioTypes.h
    src/TestRadioBackend.cpp
    src/TestRadioBackend.h
    src/TestIqSource.cpp
    src/TestIqSource.h
    src/TestSignalProcessor.cpp
    src/TestSignalProcessor.h
    src/VitaPacketBuilder.cpp
    src/VitaPacketBuilder.h
    src/VitaUdpStreamer.cpp
    src/VitaUdpStreamer.h
)

target_link_libraries(hiqsdr-flex6xxx PRIVATE Qt6::Core Qt6::Network)

install(TARGETS hiqsdr-flex6xxx)
