cmake_minimum_required(VERSION 3.20)
project(FluidSimulation VERSION 1.0)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FLUIDSIM_ENABLE_SANITIZERS
       "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FLUIDSIM_BUILD_APP "Build the OpenGL simulator application" ON)

include(CTest)

add_library(FluidCore STATIC
    src/FluidSim.cpp
    src/SimulationUtils.cpp
)
target_include_directories(FluidCore PUBLIC ${PROJECT_SOURCE_DIR}/include)

if(FLUIDSIM_BUILD_APP)
    find_package(OpenGL REQUIRED)
    find_package(GLEW REQUIRED)
    find_package(glfw3 REQUIRED)

    add_executable(${PROJECT_NAME}
        src/main.cpp
        src/Renderer.cpp
    )
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${OPENGL_INCLUDE_DIRS}
        ${GLEW_INCLUDE_DIRS}
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE
        FluidCore
        ${OPENGL_LIBRARIES}
        GLEW::GLEW
        glfw
    )
endif()

function(fluid_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(fluid_enable_sanitizers target)
    if(FLUIDSIM_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target} PRIVATE
                -O1
                -fsanitize=address,undefined
                -fno-sanitize-recover=undefined
                -fno-omit-frame-pointer
            )
            set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS
                         " -fsanitize=address,undefined")
        else()
            message(WARNING "FLUIDSIM_ENABLE_SANITIZERS is unsupported by this compiler")
        endif()
    endif()
endfunction()

fluid_enable_warnings(FluidCore)
fluid_enable_sanitizers(FluidCore)

if(FLUIDSIM_BUILD_APP)
    fluid_enable_warnings(${PROJECT_NAME})
    fluid_enable_sanitizers(${PROJECT_NAME})
endif()

if(BUILD_TESTING)
    add_executable(FluidSimulationTests tests/FluidSimulationTests.cpp)
    target_link_libraries(FluidSimulationTests PRIVATE FluidCore)
    fluid_enable_warnings(FluidSimulationTests)
    fluid_enable_sanitizers(FluidSimulationTests)
    add_test(NAME FluidSimulationTests COMMAND FluidSimulationTests)
endif()

if(FLUIDSIM_BUILD_APP)
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
endif()

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build application: ${FLUIDSIM_BUILD_APP}")
if(FLUIDSIM_BUILD_APP)
    message(STATUS "OpenGL found: ${OPENGL_FOUND}")
    message(STATUS "GLEW found: ${GLEW_FOUND}")
endif()
message(STATUS "Build tests: ${BUILD_TESTING}")
message(STATUS "Sanitizers enabled: ${FLUIDSIM_ENABLE_SANITIZERS}")
