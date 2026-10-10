#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "antlr4-runtime.h"
#include "RxLexer.h"
#include "RxParser.h"
#include "AstBuilder.h"
#include "ast/AstDump.h"

namespace {

    struct Options {
        std::string stage = "ast";
        std::string input;
        std::string output;
        bool dumpAst = false;
    };

    void printUsage() {
        std::cerr << "usage: rxcc [--stage ast|semantic] [--dump-ast] [-o <file>] <source.rx>\n";
    }

    bool parseArguments(int argc, char** argv, Options& opt) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--stage") {
                if (++i >= argc) return false;
                opt.stage = argv[i];
            } else if (arg == "--dump-ast") {
                opt.dumpAst = true;
            } else if (arg == "-o") {
                if (++i >= argc) return false;
                opt.output = argv[i];
            } else if (arg == "-h" || arg == "--help") {
                printUsage();
                std::exit(0);
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "unknown option: " << arg << "\n";
                return false;
            } else if (opt.input.empty()) {
                opt.input = arg;
            } else {
                std::cerr << "unexpected extra argument: " << arg << "\n";
                return false;
            }
        }
        return !opt.input.empty();
    }

} // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parseArguments(argc, argv, opt)) {
        printUsage();
        return 1;
    }
    if (opt.stage != "ast" && opt.stage != "semantic") {
        std::cerr << "unknown stage: " << opt.stage << "\n";
        return 1;
    }

    const bool semanticStage = (opt.stage == "semantic");

    std::ifstream in(opt.input);
    if (!in) {
        std::cerr << "cannot open " << opt.input << "\n";
        return 1;
    }

    antlr4::ANTLRInputStream input(in);
    RxLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    RxParser parser(&tokens);

    auto* tree = parser.crate();

    // 词法错误（非法 token）和语法错误都要检查。
    if (lexer.getNumberOfSyntaxErrors() != 0 || parser.getNumberOfSyntaxErrors() != 0) {
        std::cerr << "syntax error in " << opt.input << "\n";
        return semanticStage ? 1 : 2;  // 语义阶段 1=拒绝；AST 阶段 2=语法错误
    }

    rx::AstBuilder builder(parser);
    auto crate = builder.buildCrate(tree);

    if (opt.stage == "ast" || opt.dumpAst) {
        if (!opt.output.empty()) {
            std::ofstream out(opt.output);
            if (!out) {
                std::cerr << "cannot write " << opt.output << "\n";
                return 1;
            }
            rx::ast::dumpCrate(out, crate);
        } else {
            rx::ast::dumpCrate(std::cout, crate);
        }
    }

    // TODO(semantic): 在此接入 SemanticAnalyzer，按 0=接受 / 1=拒绝 返回。
    return 0;
}
