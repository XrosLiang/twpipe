PROJECT(twpipe)
cmake_minimum_required(VERSION 2.8)

set(CMAKE_MACOSX_RPATH 1)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# DYNET uses Eigen which exploits modern CPU architectures. To get the
# best possible performance, the following are recommended:
#   1. use very recent versions of gcc or Clang to build
#   2. use very recent versions of Eigen (ideally the dev version)
#   3. try compiler options like -march=native or other architecture
#      flags (the compiler does not always make the best configuration
#      decisions without help)

function(find_mkl)
  set(MKL_ARCH intel64)
  find_path(MKL_INCLUDE_DIR mkl.h
            PATHS ${MKL_ROOT} ${MKL_ROOT}/include)
  find_library(MKL_CORE_LIB NAMES mkl_intel_lp64 mkl_intel_thread mkl_core
               PATHS ${MKL_ROOT} ${MKL_ROOT}/lib/${MKL_ARCH}
               DOC "MKL core library path")

  find_library(MKL_COMPILER_LIB NAMES iomp5 libiomp5md
               PATHS ${MKL_ROOT} ${MKL_ROOT}/../compiler/lib/${MKL_ARCH}              #Windows
                     ${MKL_ROOT}/../compilers_and_libraries/linux/lib/${MKL_ARCH}_lin #Linux
               DOC "MKL compiler lib (for threaded MKL)")

  if(MKL_INCLUDE_DIR AND MKL_CORE_LIB AND MKL_COMPILER_LIB)
    get_filename_component(MKL_CORE_LIB_DIR ${MKL_CORE_LIB} DIRECTORY)
    get_filename_component(MKL_COMPILER_LIB_DIR ${MKL_COMPILER_LIB} DIRECTORY)
    get_filename_component(MKL_COMPILER_LIB_FILE ${MKL_COMPILER_LIB} NAME)
    message(STATUS "Found MKL\n   * include: ${MKL_INCLUDE_DIR},\n   * core library dir: ${MKL_CORE_LIB_DIR},\n   * compiler library: ${MKL_COMPILER_LIB}")

    # Due to a conflict with /MT and /MD, MSVC needs mkl_intel_lp64 linked last, or we can change individual
    # projects to use /MT (mkl_intel_lp64 linked with /MT, default MSVC projects use /MD), or we can instead
    # link to the DLL versions. For now I'm opting for this solution which seems to work with projects still
    # at their default /MD. Linux build requires the mkl_intel_lp64 to be linked first. So...:
    if(MSVC)
      set(LIBS ${LIBS} mkl_intel_thread mkl_core mkl_intel_lp64 ${MKL_COMPILER_LIB_FILE} PARENT_SCOPE)
    else()
      set(LIBS ${LIBS} mkl_intel_lp64 mkl_intel_thread mkl_core ${MKL_COMPILER_LIB_FILE} PARENT_SCOPE)
    endif()
    include_directories(${MKL_INCLUDE_DIR})
    link_directories(${MKL_CORE_LIB_DIR} ${MKL_COMPILER_LIB_DIR})
    set(MKL_LINK_DIRS ${MKL_CORE_LIB_DIR} ${MKL_COMPILER_LIB_DIR} PARENT_SCOPE) # Keeping this for python build
  else()
    message(FATAL_ERROR "Failed to find MKL in path: ${MKL_ROOT} (Did you set MKL_ROOT properly?)")
  endif()
endfunction()

######## Cross-compiler, cross-platform options
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEIGEN_FAST_MATH")
if (MKL OR MKL_ROOT)
  find_mkl()  # sets include/lib directories and sets ${LIBS} needed for linking
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEIGEN_USE_MKL_ALL")
endif()


######## Platform-specific options
if(WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNOMINMAX")   # Disable min/max macros in windef.h
endif()

######## Compiler-specific options
if(MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W1 -DEIGEN_HAS_C99_MATH /MP")   # -Wall produces 20k warnings
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -funroll-loops -Wall -Wno-missing-braces -std=c++11 -Ofast -g -march=native")
endif()

enable_testing()

function(find_cudnn)
  set(CUDNN_ROOT "" CACHE PATH "CUDNN root path")
  find_path(CUDNN_INCLUDE_DIRS cudnn.h
    PATHS ${CUDNN_ROOT}
          ${CUDNN_ROOT}/include
    DOC "CUDNN include path")
  find_library(CUDNN_LIBRARIES NAMES libcudnn.so
    PATHS ${CUDNN_ROOT}
          ${CUDNN_ROOT}/lib
          ${CUDNN_ROOT}/lib64
    DOC "CUDNN library path")
  if(CUDNN_INCLUDE_DIRS AND CUDNN_LIBRARIES)
    set(CUDNN_FOUND TRUE PARENT_SCOPE)
    message(STATUS "Found CUDNN (include: ${CUDNN_INCLUDE_DIRS}, library: ${CUDNN_LIBRARIES})")
    mark_as_advanced(CUDNN_INCLUDE_DIRS CUDNN_LIBRARIES)
  else()
    MESSAGE(FATAL_ERROR "Failed to find CUDNN in path: ${CUDNN_ROOT} (Did you set CUDNN_ROOT properly?)")
  endif()
endfunction()

# look for Boost
if(DEFINED ENV{BOOST_ROOT})
  set(Boost_NO_SYSTEM_PATHS ON)
  get_filename_component(Boost_INCLUDE_DIR "${Boost_INCLUDE_DIR}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
set(Boost_REALPATH ON)
message("-- Boost dir is " ${Boost_INCLUDE_DIR})
if (MSVC)
    find_package(Boost COMPONENTS program_options regex serialization REQUIRED)
else()
    add_definitions (-DBOOST_LOG_DYN_LINK)
    find_package(Boost COMPONENTS program_options regex serialization log_setup log thread system REQUIRED)
endif()
include_directories(${Boost_INCLUDE_DIR})
if(MSVC)
  # Boost does auto-linking when using a compiler like Microsoft Visual C++, we just need to help it find the libraries
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /LIBPATH:${Boost_LIBRARY_DIRS}")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /LIBPATH:${Boost_LIBRARY_DIRS}")
else()
  set(LIBS ${LIBS} ${Boost_LIBRARIES})
endif()
# trouble shooting:
# if boost library cannot be found, in addition to install boost library
# check if environment variables are set
#
# to set boost root and its library root in environment variable, use
# for example
# echo "export BOOST_LIBRARYDIR=/usr/local/lib" >> ~/.bashrc
# echo "export BOOST_ROOT=/cygdrive/d/tools/boost_1_58_0/boost_1_58_0" >> ~/.bashrc
# then run source ~/.bashrc to have those environment variable effective immediately

if(BACKEND)
  message("-- BACKEND: ${BACKEND}")
else()
  message("-- BACKEND not specified, defaulting to eigen.")
  set(BACKEND "eigen")
endif()

if(BACKEND MATCHES "^eigen$")
  set(WITH_EIGEN_BACKEND 1)
elseif(BACKEND MATCHES "^cuda$")
  set(WITH_CUDA_BACKEND 1)
else()
  message(SEND_ERROR "BACKEND must be eigen or cuda")
endif()

if (WITH_CUDA_BACKEND)
  find_package(CUDA REQUIRED)
  set(CUDA_TOOLKIT_ROOT_DIR ${CUDA_ROOT})
  include_directories(SYSTEM ${CUDA_INCLUDE_DIRS})
  #list(APPEND CUDA_LIBRARIES /usr/lib64/libpthread.so)
  MESSAGE("CUDA_LIBRARIES: ${CUDA_LIBRARIES}")
  list(REMOVE_ITEM CUDA_LIBRARIES -lpthread)
  set(LIBS ${LIBS} ${CUDA_LIBRARIES})
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DEIGEN_HAS_CUDA_FP16 -DEIGEN_USE_GPU")
  #find_cudnn()
  #include_directories(SYSTEM ${CUDNN_INCLUDE_DIRS})
endif()

# look for Eigen
find_package(Eigen3 REQUIRED)
get_filename_component(EIGEN3_INCLUDE_DIR "${EIGEN3_INCLUDE_DIR}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
message("-- Eigen dir is " ${EIGEN3_INCLUDE_DIR})
include_directories(${EIGEN3_INCLUDE_DIR})

FIND_PACKAGE(Threads REQUIRED)
set(LIBS ${LIBS} ${CMAKE_THREAD_LIBS_INIT})

#configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
#set(source_directory ${PROJECT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dynet)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/dynet_layer)

add_subdirectory(dynet/dynet)

set(DYNET_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/dynet)
add_subdirectory(dynet_layer/dynet_layer)

add_subdirectory(src)
