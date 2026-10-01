cmake_minimum_required(VERSION 3.20)
project(rxcc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Java REQUIRED COMPONENTS Runtime)
find_package(antlr4-runtime REQUIRED)   # 由 1.3 安装提供

set(ANTLR_JAR "$ENV{HOME}/tools/antlr-4.13.2-complete.jar" CACHE FILEPATH "ANTLR tool jar")
set(GEN_DIR "${CMAKE_BINARY_DIR}/gen")

set(LEXER_G4  "${CMAKE_CURRENT_SOURCE_DIR}/grammar/RxLexer.g4")
set(PARSER_G4 "${CMAKE_CURRENT_SOURCE_DIR}/grammar/RxParser.g4")

set(GENERATED
        ${GEN_DIR}/RxLexer.cpp ${GEN_DIR}/RxLexer.h
        ${GEN_DIR}/RxParser.cpp ${GEN_DIR}/RxParser.h
        ${GEN_DIR}/RxParserBaseVisitor.cpp ${GEN_DIR}/RxParserBaseVisitor.h
        ${GEN_DIR}/RxParserVisitor.h)

add_custom_command(
        OUTPUT ${GENERATED}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${GEN_DIR}
        COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR_JAR} -Dlanguage=Cpp -visitor -no-listener
        -o ${GEN_DIR} ${LEXER_G4}       # 必须先 Lexer
        COMMAND ${Java_JAVA_EXECUTABLE} -jar ${ANTLR_JAR} -Dlanguage=Cpp -visitor -no-listener
        -o ${GEN_DIR} ${PARSER_G4}      # 再 Parser（依赖 tokenVocab）
        DEPENDS ${LEXER_G4} ${PARSER_G4}
        COMMENT "Generating Rx Lexer/Parser with ANTLR"
        VERBATIM)

add_executable(rxcc src/main.cpp ${GENERATED})
target_include_directories(rxcc PRIVATE ${GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(rxcc PRIVATE antlr4_static)