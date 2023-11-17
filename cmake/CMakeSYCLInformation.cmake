if(NOT CMAKE_SYCL_COMPILE_OPTIONS_PIC)
  set(CMAKE_SYCL_COMPILE_OPTIONS_PIC ${CMAKE_CXX_COMPILE_OPTIONS_PIC})
endif()

if(NOT CMAKE_SYCL_COMPILE_OPTIONS_PIE)
  set(CMAKE_SYCL_COMPILE_OPTIONS_PIE ${CMAKE_CXX_COMPILE_OPTIONS_PIE})
endif()
if(NOT CMAKE_SYCL_LINK_OPTIONS_PIE)
  set(CMAKE_SYCL_LINK_OPTIONS_PIE ${CMAKE_CXX_LINK_OPTIONS_PIE})
endif()
if(NOT CMAKE_SYCL_LINK_OPTIONS_NO_PIE)
  set(CMAKE_SYCL_LINK_OPTIONS_NO_PIE ${CMAKE_CXX_LINK_OPTIONS_NO_PIE})
endif()

if(NOT CMAKE_SYCL_OUTPUT_EXTENSION)
  set(CMAKE_SYCL_OUTPUT_EXTENSION ${CMAKE_CXX_OUTPUT_EXTENSION})
endif()

if(NOT CMAKE_INCLUDE_FLAG_SYCL)
  set(CMAKE_INCLUDE_FLAG_SYCL ${CMAKE_INCLUDE_FLAG_CXX})
endif()

if(NOT CMAKE_SYCL_COMPILE_OPTIONS_EXPLICIT_LANGUAGE)
  set(CMAKE_SYCL_COMPILE_OPTIONS_EXPLICIT_LANGUAGE ${CMAKE_CXX_COMPILE_OPTIONS_EXPLICIT_LANGUAGE})
endif()

if(NOT CMAKE_SYCL_DEPENDS_USE_COMPILER)
  set(CMAKE_SYCL_DEPENDS_USE_COMPILER ${CMAKE_CXX_DEPENDS_USE_COMPILER})
endif()

if(NOT CMAKE_DEPFILE_FLAGS_SYCL)
  set(CMAKE_DEPFILE_FLAGS_SYCL ${CMAKE_DEPFILE_FLAGS_CXX})
endif()

if(NOT CMAKE_SYCL_DEPFILE_FORMAT)
  set(CMAKE_SYCL_DEPFILE_FORMAT ${CMAKE_CXX_DEPFILE_FORMAT})
endif()

if(NOT CMAKE_SYCL_COMPILE_OBJECT)
  set(CMAKE_SYCL_COMPILE_OBJECT "<CMAKE_SYCL_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
endif()

if(NOT CMAKE_SYCL_LINK_EXECUTABLE)
  set(CMAKE_SYCL_LINK_EXECUTABLE "<CMAKE_SYCL_COMPILER> <FLAGS> <CMAKE_SYCL_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
endif()

set(CMAKE_SYCL_INFORMATION_LOADED 1)
