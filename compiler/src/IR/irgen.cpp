#include "IR/irgen.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace rx::ir {
    namespace {
        // 解析整数字面量。第一步只要求十进制；后缀（i32/u32/...）直接忽略。
        long long intValue(const std::string& text) {
            std::size_t end = 0;
            while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
                ++end;
            }
            std::string digits = text.substr(0, end);
            digits.erase(std::remove(digits.begin(), digits.end(), '_'), digits.end());
            if (digits.empty()) {
                return 0;
            }
            try {
                return std::stoll(digits);
            } catch (...) {
                return 0;
            }
        }
    }

    void IrGen::emitCrate(const ast::Crate& crate) {
        line("; ModuleID = 'rx'");
        line("source_filename = \"rx\"");
        line("");

        // printf("%d\n") 和 printf("%d") 的格式串
        line("@fmt_i32_nl = private unnamed_addr constant [4 x i8] c\"%d\\0A\\00\"");
        line("@fmt_i32    = private unnamed_addr constant [3 x i8] c\"%d\\00\"");
        line("");

        // REIMU 提供 C libc 的 printf
        line("declare i32 @printf(ptr, ...)");
        line("");

        bool hasMain = false;

        for (auto& item : crate.items) {
            auto* fn = dynamic_cast<const ast::FunctionDef*>(item.get());
            if (!fn) continue;

            // 第一步只处理 main；其它函数先不生成
            if (fn->name == "main") {
                emitFunction(*fn);
                hasMain = true;
            }
        }

        // REIMU 从全局 main 启动；用户 main 被改名为 rx_main
        if (hasMain) {
            line("define i32 @main() {");
            line("entry:");
            line("  call void @rx_main()");
            line("  ret i32 0");
            line("}");
        }
    }

    void IrGen::emitFunction(const ast::FunctionDef& fn) {
        // 第一步只支持无参函数
        if (fn.params.selfParam || !fn.params.params.empty()) return;

        const std::string name = "rx_" + fn.name;
        line("define void @" + name + "() {");
        line("entry:");

        if (fn.body) {
            for (auto& stmt : fn.body->stmts) {
                emitStmt(*stmt);
            }
            // 尾表达式第一步先不处理
        }

        line("  ret void");
        line("}");
        line("");
    }

    void IrGen::emitStmt(const ast::Stmt& s) {
        auto* es = dynamic_cast<const ast::ExprStmt*>(&s);
        if (!es) return;

        auto* call = dynamic_cast<const ast::Call*>(es->expr.get());
        if (!call) return;

        emitBuiltinCall(*call);
    }

    bool IrGen::emitBuiltinCall(const ast::Call& call) {
        auto* path = dynamic_cast<const ast::Path*>(call.target.get());
        if (!path || path->segments.size() != 1) return false;

        const std::string& name = path->segments[0].ident.text;
        if (name != "println_i32" && name != "print_i32") return false;
        if (call.args.size() != 1) return false;

        auto* lit = dynamic_cast<const ast::Literal*>(call.args[0].get());
        if (!lit || lit->kind != ast::LiteralKind::Integer) return false;

        const std::string fmt = (name == "println_i32") ? "fmt_i32_nl" : "fmt_i32";
        const std::string t = tmp();

        line("  " + t + " = call i32 (ptr, ...) @printf(ptr @" + fmt +
             ", i32 " + std::to_string(intValue(lit->text)) + ")");
        return true;
    }
}