cmake_minimum_required(VERSION 3.20)
project(rxcc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(antlr4-runtime REQUIRED)

# ANTLR 生成的 Lexer/Parser 源码已提交到 compiler/generated/。
# 这样构建不依赖 Java 和 antlr-4.13.2-complete.jar，满足评测机的离线环境。
set(GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
set(GENERATED
        ${GEN_DIR}/RxLexer.cpp ${GEN_DIR}/RxLexer.h
        ${GEN_DIR}/RxParser.cpp ${GEN_DIR}/RxParser.h
        ${GEN_DIR}/RxParserBaseVisitor.cpp ${GEN_DIR}/RxParserBaseVisitor.h
        ${GEN_DIR}/RxParserVisitor.cpp ${GEN_DIR}/RxParserVisitor.h)

add_executable(rxcc
        src/main.cpp
        src/AstBuilder.cpp
        src/ast/AstDump.cpp
        src/semantic/Sema.cpp
        src/semantic/ConstEval.cpp
        src/semantic/Layout.cpp
        src/semantic/Traits.cpp
        ${GENERATED})

target_include_directories(rxcc PRIVATE ${GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 兼容不同发行版导出的 ANTLR C++ runtime target 名称
if(TARGET antlr4_static)
    target_link_libraries(rxcc PRIVATE antlr4_static)
elseif(TARGET antlr4-runtime)
    target_link_libraries(rxcc PRIVATE antlr4-runtime)
elseif(TARGET antlr4_shared)
    target_link_libraries(rxcc PRIVATE antlr4_shared)
else()
    message(FATAL_ERROR "ANTLR4 C++ runtime target not found")
endif()
