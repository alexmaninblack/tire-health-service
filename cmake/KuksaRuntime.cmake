# SPDX-FileCopyrightText: 2026 maninblack
# SPDX-License-Identifier: Apache-2.0

# Product builds explicitly enable this target. No downloaded dependencies or
# diagnostic executable is substituted when the requested toolchain is absent.
find_package(gRPC 1.60.1 EXACT CONFIG REQUIRED)
find_package(Protobuf 25.8.0 EXACT CONFIG REQUIRED)
set(THS_KUKSA_SOURCE_ROOT "" CACHE PATH "Pinned KUKSA databroker 0.5.0 source checkout")
if(NOT EXISTS "${THS_KUKSA_SOURCE_ROOT}/proto/kuksa/val/v1/val.proto")
    message(FATAL_ERROR "THS_KUKSA_SOURCE_ROOT must contain the pinned upstream VAL schemas")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${THS_KUKSA_SOURCE_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE kuksa_revision OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE revision_error)
if(NOT revision_error EQUAL 0 OR NOT kuksa_revision STREQUAL "30e5c13abc496d0b39aaa6c25acebb088b9902e3")
    message(FATAL_ERROR "KUKSA source revision is not the accepted 0.5.0 pin")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${THS_KUKSA_SOURCE_ROOT}" diff --exit-code HEAD -- proto/kuksa/val/v1
    RESULT_VARIABLE kuksa_dirty)
if(NOT kuksa_dirty EQUAL 0)
    message(FATAL_ERROR "KUKSA VAL schemas have local modifications")
endif()
file(SHA256 "${THS_KUKSA_SOURCE_ROOT}/proto/kuksa/val/v1/val.proto" val_sha256)
file(SHA256 "${THS_KUKSA_SOURCE_ROOT}/proto/kuksa/val/v1/types.proto" types_sha256)
if(NOT val_sha256 STREQUAL "8ab682d850e70f9687ad1f91493570728b5f2ddf1ab5929f4ebc4202836f642e" OR
   NOT types_sha256 STREQUAL "a0772a0877eb6efceb98b49eeb6ea531ab18ebd6d83abdc76e63ed169ac0481c")
    message(FATAL_ERROR "KUKSA VAL source content hash mismatch")
endif()
set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated}")
add_custom_command(
    OUTPUT "${generated}/kuksa/val/v1/val.pb.cc" "${generated}/kuksa/val/v1/val.pb.h"
           "${generated}/kuksa/val/v1/val.grpc.pb.cc" "${generated}/kuksa/val/v1/val.grpc.pb.h"
           "${generated}/kuksa/val/v1/types.pb.cc" "${generated}/kuksa/val/v1/types.pb.h"
    COMMAND protobuf::protoc "--proto_path=${THS_KUKSA_SOURCE_ROOT}/proto"
        "--cpp_out=${generated}" "--grpc_out=${generated}"
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        kuksa/val/v1/val.proto kuksa/val/v1/types.proto
    DEPENDS "${THS_KUKSA_SOURCE_ROOT}/proto/kuksa/val/v1/val.proto"
            "${THS_KUKSA_SOURCE_ROOT}/proto/kuksa/val/v1/types.proto"
    VERBATIM)
add_library(tire_health_kuksa_proto
    "${generated}/kuksa/val/v1/val.pb.cc"
    "${generated}/kuksa/val/v1/val.grpc.pb.cc"
    "${generated}/kuksa/val/v1/types.pb.cc")
target_include_directories(tire_health_kuksa_proto PUBLIC "${generated}")
target_link_libraries(tire_health_kuksa_proto PUBLIC gRPC::grpc++ protobuf::libprotobuf)
add_executable(tire-health-service src/runtime/grpc_main.cpp)
target_link_libraries(tire-health-service PRIVATE tire_health_runtime tire_health_kuksa_proto)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(tire-health-service PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
install(TARGETS tire-health-service tire-health-bootstrap RUNTIME DESTINATION bin)
