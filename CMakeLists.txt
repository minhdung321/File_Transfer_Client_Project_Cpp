# CMakeList.txt : CMake project for File_Transfer_Client_Project_Cpp, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("File_Transfer_Client_Project_Cpp")

# Include directories
include_directories("include")

# Get all header files in include folder
file(GLOB_RECURSE HEADER_FILES "include/*.h" "include/*.hpp")

# Get all source files in src folder
file(GLOB_RECURSE SOURCE_FILES "src/*.cpp")

# Add source to this project's executable.
add_executable (
	${PROJECT_NAME} 
	"File_Transfer_Client_Project_Cpp.cpp" 
	${HEADER_FILES}
	${SOURCE_FILES})

# Include OpenSSL libraries
set (OPENSSL_USE_STATIC_LIBS TRUE)
set (OPENSSL_MSVC_STATIC_RT TRUE)
set (OPENSSL_ROOT_DIR "${CMAKE_SOURCE_DIR}/lib/openssl")
set (OPENSSL_INCLUDE "${OPENSSL_ROOT_DIR}/include")
set (OPENSSL_LIBRARIES "${OPENSSL_ROOT_DIR}/lib")

target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE})
target_link_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_LIBRARIES})

if (WIN32)
  target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
  target_link_libraries(${PROJECT_NAME} PRIVATE Crypt32)
endif()

# Link OpenSSL libraries
target_link_libraries(${PROJECT_NAME} PRIVATE libssl libcrypto)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET File_Transfer_Client_Project_Cpp PROPERTY CXX_STANDARD 20)
endif()

# TODO: Add tests and install targets if needed.
