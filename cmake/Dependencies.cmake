set(Shadow_LINKER_LIBS "")

find_package(Protobuf QUIET)
if (Protobuf_FOUND)
  include(cmake/ProtoBuf.cmake)
  include_directories(SYSTEM ${Protobuf_INCLUDE_DIRS})
  message(STATUS "Found Protobuf: ${Protobuf_INCLUDE_DIRS}, ${Protobuf_LIBRARIES} (found version ${Protobuf_VERSION})")
  message(STATUS "Found Protoc: ${Protoc_EXECUTABLE} (found version ${Protoc_VERSION})")
  if (${USE_Protobuf})
    add_definitions(-DUSE_Protobuf)
  endif ()
endif ()

if (${BUILD_SERVICE})
  find_package(gRPC QUIET)
  if (gRPC_FOUND)
    message(STATUS "Found gRPC: ${gRPC_INCLUDE_DIRS}, ${gRPC_LIBRARIES}")
    message(STATUS "Found gRPC cpp plugin: ${gRPC_CPP_PLUGIN}")
  else ()
    message(WARNING "Could not find gRPC, disable it")
  endif ()
endif ()

if (${USE_GLog})
  list(APPEND Shadow_LINKER_LIBS glog)
  add_definitions(-DUSE_GLog)
endif ()

if (${USE_OpenCV})
  find_package(OpenCV PATHS ${OpenCV_DIR} NO_DEFAULT_PATH QUIET COMPONENTS core highgui imgproc imgcodecs videoio)
  if (NOT OpenCV_FOUND) # if not OpenCV 3.x, then try to find OpenCV 2.x in default path
    find_package(OpenCV REQUIRED COMPONENTS core highgui imgproc)
  endif ()
  include_directories(SYSTEM ${OpenCV_INCLUDE_DIRS})
  list(APPEND Shadow_LINKER_LIBS ${OpenCV_LIBS})
  message(STATUS "Found OpenCV: ${OpenCV_CONFIG_PATH} (found version ${OpenCV_VERSION})")
  add_definitions(-DUSE_OpenCV)
endif ()

if (${USE_CUDA})
  find_package(CUDA QUIET)
  if (CUDA_FOUND)
    include(cmake/Cuda.cmake)
    shadow_select_nvcc_arch_flags(NVCC_FLAGS_EXTRA)
    set(CUDA_PROPAGATE_HOST_FLAGS ON)
    list(APPEND CUDA_NVCC_FLAGS ${NVCC_FLAGS_EXTRA})
    include_directories(SYSTEM ${CUDA_TOOLKIT_INCLUDE})
    list(APPEND Shadow_LINKER_LIBS ${CUDA_CUDART_LIBRARY} ${CUDA_cublas_LIBRARY})
    message(STATUS "Found CUDA: ${CUDA_TOOLKIT_ROOT_DIR} (found version ${CUDA_VERSION})")
    message(STATUS "Added CUDA NVCC flags: ${NVCC_FLAGS_EXTRA}")
    add_definitions(-DUSE_CUDA)
    if (${USE_CUDNN})
      find_package(CUDNN QUIET)
      if (CUDNN_FOUND)
        include_directories(SYSTEM ${CUDNN_INCLUDE_DIRS})
        list(APPEND Shadow_LINKER_LIBS ${CUDNN_LIBRARIES})
        message(STATUS "Found CUDNN: ${CUDNN_INCLUDE_DIRS}, ${CUDNN_LIBRARIES} (found version ${CUDNN_VERSION})")
        add_definitions(-DUSE_CUDNN)
      else ()
        message(WARNING "Could not find CUDNN, disable it")
      endif ()
    endif ()
  else ()
    message(WARNING "Could not find CUDA, using CPU")
  endif ()
elseif (${USE_CL})
  find_package(OpenCL QUIET)
  find_package(clBLAS QUIET)
  if (OpenCL_FOUND AND clBLAS_FOUND)
    include_directories(SYSTEM ${OpenCL_INCLUDE_DIRS} ${clBLAS_INCLUDE_DIRS})
    list(APPEND Shadow_LINKER_LIBS ${OpenCL_LIBRARIES} ${clBLAS_LIBRARIES})
    message(STATUS "Found OpenCL: ${OpenCL_INCLUDE_DIRS}, ${OpenCL_LIBRARIES} (found version ${OpenCL_VERSION_STRING})")
    message(STATUS "Found clBLAS: ${clBLAS_INCLUDE_DIRS}, ${clBLAS_LIBRARIES} (found version ${clBLAS_VERSION})")
    add_definitions(-DUSE_CL)
  else ()
    message(WARNING "Could not find OpenCL or clBLAS, using CPU")
  endif ()
endif ()

if (NOT CUDA_FOUND AND NOT OpenCL_FOUND)
  if (${USE_Eigen})
    find_package(Eigen QUIET)
    if (Eigen_FOUND)
      include_directories(SYSTEM ${Eigen_INCLUDE_DIRS})
      message(STATUS "Found Eigen: ${Eigen_INCLUDE_DIRS} (found version ${Eigen_VERSION})")
      add_definitions(-DUSE_Eigen)
    else ()
      message(WARNING "Could not find Eigen, disable it")
    endif ()
  endif ()
  if (${USE_BLAS})
    if (${BLAS} STREQUAL "OpenBLAS" OR ${BLAS} STREQUAL "openblas")
      find_package(OpenBLAS QUIET)
      if (OpenBLAS_FOUND)
        include_directories(SYSTEM ${OpenBLAS_INCLUDE_DIRS})
        list(APPEND Shadow_LINKER_LIBS ${OpenBLAS_LIBRARIES})
        if (NOT MSVC)
          list(APPEND Shadow_LINKER_LIBS pthread)
        endif ()
        message(STATUS "Found OpenBLAS: ${OpenBLAS_INCLUDE_DIRS}, ${OpenBLAS_LIBRARIES} (found version ${OpenBLAS_VERSION})")
        add_definitions(-DUSE_OpenBLAS)
      else ()
        message(WARNING "Could not find OpenBLAS, disable it")
      endif ()
    elseif (${BLAS} STREQUAL "MKL" OR ${BLAS} STREQUAL "mkl")
      find_package(MKL QUIET)
      if (MKL_FOUND)
        include_directories(SYSTEM ${MKL_INCLUDE_DIRS})
        list(APPEND Shadow_LINKER_LIBS ${MKL_LIBRARIES})
        message(STATUS "Found MKL: ${MKL_INCLUDE_DIRS}, ${MKL_LIBRARIES} (found version ${MKL_VERSION})")
        add_definitions(-DUSE_MKL)
      else ()
        message(WARNING "Could not find MKL, disable it")
      endif ()
    endif ()
  endif ()
  if (${USE_NNPACK})
    find_package(NNPACK QUIET)
    if (NNPACK_FOUND)
      include_directories(SYSTEM ${NNPACK_INCLUDE_DIRS})
      list(APPEND Shadow_LINKER_LIBS ${NNPACK_LIBRARIES})
      if (NOT MSVC)
        list(APPEND Shadow_LINKER_LIBS pthread)
      endif ()
      message(STATUS "Found NNPACK: ${NNPACK_INCLUDE_DIRS}, ${NNPACK_LIBRARIES}")
      add_definitions(-DUSE_NNPACK)
    else ()
      message(WARNING "Could not find NNPACK, disable it")
    endif ()
  endif ()
endif ()
