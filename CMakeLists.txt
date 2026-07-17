cmake_minimum_required(VERSION 3.20)
project(FluidSimulation VERSION 1.0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)

set(FLUID_SOURCES src/FluidSim.cpp src/Renderer.cpp)
set(FLUID_LIBRARIES OpenGL::GL GLEW::GLEW glfw)

add_executable(${PROJECT_NAME} src/main.cpp ${FLUID_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE ${FLUID_LIBRARIES})

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(FluidSimulationTests
        tests/basic_tests.cpp
        tests/projection_regression.cpp
        src/main.cpp
        ${FLUID_SOURCES}
    )
    target_compile_definitions(FluidSimulationTests PRIVATE FLUIDSIM_TEST_BUILD)
    target_include_directories(FluidSimulationTests PRIVATE include)
    target_link_libraries(FluidSimulationTests PRIVATE ${FLUID_LIBRARIES})
    if(MSVC)
        target_compile_options(FluidSimulationTests PRIVATE /W4)
    else()
        target_compile_options(FluidSimulationTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME FluidSimulationTests COMMAND FluidSimulationTests)
endif()

install(TARGETS ${PROJECT_NAME} DESTINATION bin)
