#include <fstream>
#include <iostream>
#include "antlr4-runtime.h"
#include "RxLexer.h"
#include "RxParser.h"

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: rxcc <file.rx>\n"; return 1; }
    std::ifstream in(argv[1]);
    if (!in) { std::cerr << "cannot open " << argv[1] << "\n"; return 1; }

    antlr4::ANTLRInputStream input(in);
    RxLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    RxParser parser(&tokens);

    auto* tree = parser.crate();                 // 入口规则是 crate
    std::cout << tree->toStringTree(&parser) << "\n";
    return parser.getNumberOfSyntaxErrors() ? 2 : 0;
}