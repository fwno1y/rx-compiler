#include "Layout.h"
#include <functional>

namespace rx::sema {
    namespace {
        // 收集一个类型在“不打断布局”的前提下能直接到达的结构体。
        // Box/Vec/Ref 在这里直接返回：它们的大小固定，环在此被打破。
        void inlineReach(const TyPtr& t, const std::function<void(const std::string&)>& onStruct) {
            if (!t) {
                return;
            }
            switch (t->kind) {
                case Ty::Kind::Struct:
                    onStruct(t->name);
                    return;
                case Ty::Kind::Array:
                    inlineReach(t->element, onStruct);   // 内联数组继续深入
                    return;
                case Ty::Kind::Box:
                case Ty::Kind::Vec:
                case Ty::Kind::Ref:
                case Ty::Kind::Int:
                case Ty::Kind::Bool:
                case Ty::Kind::Unit:
                case Ty::Kind::Never:
                case Ty::Kind::Fn:
                case Ty::Kind::Error:
                    return;                              // 间接或叶子，打断
            }
        }

    }

    void Layout::check() {
        for (auto& entry : symbols_.structs()) {
            if (color_[entry.first] == Color::White) {
                visit(entry.first);
            }
        }
    }

    void Layout::visit(const std::string& name) {
        auto* info = symbols_.findStruct(name);
        if (!info || !info->def) {
            return;
        }
        color_[name] = Color::Gray;                  // 当前 DFS 路径
        for (auto& fld : info->def->fields) {
            auto it = info->fieldTypes.find(fld.name);
            if (it == info->fieldTypes.end() || !it->second) {
                continue;
            }
            inlineReach(it->second, [&](const std::string& target) {
                if (target == name || color_[target] == Color::Gray) {
                    // 回到当前路径上的结点 => 内联环，无限大小
                    diags_.error(fld.loc,
                                 "struct `" + name +
                                 "` has infinite size (recursive inline layout)");
                    return;
                }
                if (color_[target] == Color::White) {
                    visit(target);
                }
            });
        }
        color_[name] = Color::Black;                 // 该子树已处理完
    }

}