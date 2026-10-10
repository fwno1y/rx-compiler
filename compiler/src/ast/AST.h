#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SourceLoc.h"

namespace rx::ast {
    // 前向声明：AST 是递归结构，子节点用智能指针持有，所以可以先声明后定义。
    struct Item;
    struct Type;
    struct Expr;
    struct Stmt;
    struct UseTree;
    struct WhereClauseItem;
    struct GenericArg;
    struct Block;
    struct Crate;
    // 指针别名：集中定义所有权模型（当前为 shared_ptr），全 AST 统一使用。
    using TypePtr = std::shared_ptr<Type>;
    using ExprPtr = std::shared_ptr<Expr>;
    using StmtPtr = std::shared_ptr<Stmt>;
    using ItemPtr = std::shared_ptr<Item>;
    using UseTreePtr = std::shared_ptr<UseTree>;
    using WhereClauseItemPtr = std::shared_ptr<WhereClauseItem>;
    using GenericArgPtr = std::shared_ptr<GenericArg>;
    using BlockPtr = std::shared_ptr<Block>;
    using CratePtr = std::shared_ptr<Crate>;

    // 标识的种类：普通标识符 / self / Self / super / crate。
    enum class IdentifierKind {
        Normal,
        SelfValue,
        SelfType,
        Super,
        Crate,
    };
    // 字面量种类：整数字面量 / 布尔字面量。
    enum class LiteralKind {
        Integer,
        Bool,
    };
    // 一元运算符：-、!、*、&、&mut、&&、&&mut。
    enum class UnaryOp {
        Neg, // -
        Not, // !
        DeRef, // *
        Ref, // &
        RefMut, // &mut
        RefRef, // &&
        RefRefMut, // &&mut
    };
    // 二元运算符：算术、比较、逻辑、位运算、移位。
    enum class BinaryOp {
        Add, Sub, Mul, Div, Rem, // +,-,*,/,%
        Eq, Ne, Lt, Le, Gt, Ge, // ==,!=,<,<=,>,>=
        And, Or, // &&,||
        BitAnd, BitOr, BitXor, // &,|,^
        Shl, Shr // <<,>>
    };
    // 赋值运算符：= 与各种复合赋值 += -= *= /= %= &= |= ^= <<= >>=。
    enum class AssignOp {
        Assign,
        AddAssign,
        SubAssign,
        MulAssign,
        DivAssign,
        RemAssign,
        BitAndAssign,
        BitOrAssign,
        BitXorAssign,
        ShlAssign,
        ShrAssign,
    };

    // 所有 AST 节点的基类：带源码位置 loc，虚析构以支持多态删除。
    struct Node {
        SourceLoc loc;
        virtual ~Node() = default;
    };

    // 路径中的一段：普通标识符或 self/Self/super/crate，用 kind 区分种类。
    // 例：a::self::b 里的 a、self、b 各是一段。
    struct IdentSegment {
        SourceLoc loc;
        IdentifierKind kind = IdentifierKind::Normal;
        std::string text;
    };
    // ---------------- use 声明右侧的树 ----------------
    // use 右侧的基类。
    struct UseTree : Node {};
    // 普通路径（可带别名）：a::b、a::b as c、x as _。
    struct UseTreePath : UseTree {
        std::vector<IdentSegment> path;
        bool hasAlias = false;
        std::string alias;
        bool aliasUnderscore = false; // `use a as _;`
    };
    // 通配：a::* 或 *（hasPrefix 区分有无前缀）。
    struct UseTreeGlobal : UseTree {
        bool hasPrefix = false;
        std::vector<IdentSegment> prefix;
    };
    // 分组：a::{b, c::*}，items 递归嵌套。
    struct UseTreeGroup : UseTree {
        bool hasPrefix = false;
        std::vector<IdentSegment> prefix;
        std::vector<UseTreePtr> items;
    };

    // ---------------- 泛型与生命周期 ----------------
    // 一个生命周期名，如 'a（name 含前导单引号）。
    struct Lifetime {
        SourceLoc loc;
        std::string name;
    };
    // 生命周期上界集合，如 'a: 'b + 'c 里的 'b + 'c。
    struct LifetimeBounds {
        SourceLoc loc;
        std::vector<Lifetime> lifetimes;
    };
    // 一个生命周期参数，如 'a 或 'a: 'b。
    struct LifetimeParam {
        SourceLoc loc;
        Lifetime lifetime;
        std::optional<LifetimeBounds> bounds;
    };
    // 泛型参数表 <...>；Rx 的泛型参数只有生命周期。
    struct GenericParams {
        SourceLoc loc;
        std::vector<LifetimeParam> lifetimes;
    };
    // 类型 where 项的上界，如 T: 'a + 'b 里的 'a + 'b。
    struct TypeParamBounds {
        SourceLoc loc;
        std::vector<Lifetime> lifetimes;
    };
    // where 子句一项的基类。
    struct WhereClauseItem : Node {};
    // 生命周期形式的 where 项，如 'a: 'b + 'c。
    struct LifetimeWhereItem : WhereClauseItem {
        Lifetime lifetime;
        LifetimeBounds bounds;
    };
    // 类型形式的 where 项，如 T: 'a。
    struct TypeWhereItem : WhereClauseItem {
        TypePtr type;
        std::optional<TypeParamBounds> bounds;
    };
    // 整个 where 子句。
    struct WhereClause {
        SourceLoc loc;
        std::vector<WhereClauseItemPtr> items;
    };

    // ---------------- 函数参数相关 ----------------
    // 一个可变绑定名，如 mut x（let 与函数参数共用）。
    struct Binding {
        SourceLoc loc;
        bool isMut = false;
        std::string name;
    };
    // 方法接收者，如 self / &self / &mut self / &'a self。
    struct SelfParam {
        SourceLoc loc;
        bool isRef = false;
        bool isDoubleRef = false;
        bool isMut = false;
        std::optional<Lifetime> lifetime;
    };
    // 普通函数参数，如 x: i32。
    struct FunctionParam {
        SourceLoc loc;
        Binding binding;
        TypePtr type;
    };
    // 参数列表：可选的 self，其后是若干普通参数。
    struct ParamList {
        SourceLoc loc;
        std::optional<SelfParam> selfParam;
        std::vector<FunctionParam> params;
    };

    // ---------------- Item（顶层声明）----------------
    // 顶层声明基类。
    struct Item : Node {};
    // use 声明：use ...;
    struct UseDecl : Item {
        UseTreePtr tree;
    };
    // 属性，如 #[derive(Clone, Eq)]。
    struct Attribute {
        SourceLoc loc;
        std::vector<std::string> derives;
    };
    // 结构体字段声明，如 x: i32。
    struct StructField {
        SourceLoc loc;
        std::string name;
        TypePtr type;
    };
    // 函数定义：fn name<...>(params) -> T where ... { body }
    struct FunctionDef : Item {
        std::string name;
        std::optional<GenericParams> generics; // 无泛型则为空
        ParamList params;
        TypePtr returnType;                    // 无 -> 则为空（返回 ()）
        std::optional<WhereClause> where;      // 无 where 则为空
        BlockPtr body;
    };
    // 结构体定义：struct name<...> where ... { fields }
    struct StructDef : Item {
        std::vector<Attribute> attributes;     // 如 #[derive(...)]
        std::string name;
        std::optional<GenericParams> generics;
        std::optional<WhereClause> where;
        std::vector<StructField> fields;
    };
    // 常量定义：const NAME: T = value;
    struct ConstItem : Item {
        std::string name;
        TypePtr type;
        ExprPtr value;
    };
    // 固有 impl 块：impl<...> T where ... { items }
    struct Impl : Item {
        std::optional<GenericParams> generics;
        TypePtr target;                        // impl 的目标类型
        std::optional<WhereClause> where;
        std::vector<ItemPtr> items;            // 其中只能是 const / fn
    };

    // ---------------- 路径 ----------------
    // 泛型实参表，如 <i32, 'a>。
    struct GenericArgs {
        SourceLoc loc;
        std::vector<GenericArgPtr> args;
    };
    // 路径中带可选泛型实参的一段，如 Vec<i32>。
    struct PathSegment {
        SourceLoc loc;
        IdentSegment ident;
        std::optional<GenericArgs> generics;
    };

    // ---------------- Type（类型）----------------
    // 类型基类。
    struct Type : Node {};
    // 命名类型，如 i32 / Vec<Box<Node>> / Self。
    struct TypePath : Type {
        std::vector<PathSegment> segments;
    };
    // 引用类型，如 &T / &mut T / &&'a T。
    struct RefType : Type {
        bool isDouble = false;
        bool isMut = false;
        std::optional<Lifetime> lifetime;
        TypePtr target;
    };
    // 数组类型，如 [i32; 4] / [u8; N]（长度是表达式，可能是常量名）。
    struct ArrayType : Type {
        TypePtr element;
        ExprPtr length;
    };
    // 单元类型 ()。
    struct UnitType : Type {};
    // 括号类型，如 (S)。
    struct ParenType : Type {
        TypePtr inner;
    };

    // ---------------- Stmt（语句）----------------
    // 语句基类。
    struct Stmt : Node {};
    // let 语句：let binding: T = init;
    struct LetStmt : Stmt {
        Binding binding;
        TypePtr type;                          // 无类型标注则为空
        ExprPtr init;
    };
    // 表达式语句；hasSemi 记录是否以分号结尾（影响块是否有值）。
    struct ExprStmt : Stmt {
        ExprPtr expr;
        bool hasSemi = false;
    };

    // ---------------- Expr（表达式）----------------
    // 表达式基类。
    struct Expr : Node {};
    // 块 { ... }：stmts 是语句，tail 是尾表达式（无分号，决定块的值）。
    struct Block : Expr {
        std::vector<StmtPtr> stmts;
        ExprPtr tail;
    };
    // 字面量，如 42 / true（text 保真保存原文，后缀留给语义阶段）。
    struct Literal : Expr {
        LiteralKind kind = LiteralKind::Integer;
        std::string text;
    };
    // 单元值 ()。
    struct UnitExpr : Expr {};
    // 路径表达式，如 a::b::c。
    struct Path : Expr {
        std::vector<PathSegment> segments;
    };
    // 一元运算，如 -x / !b / *p / &x / &mut x。
    struct Unary : Expr {
        UnaryOp op = UnaryOp::Neg;
        ExprPtr operand;
    };
    // 二元运算，如 a + b / a < b / a && b。
    struct Binary : Expr {
        BinaryOp op = BinaryOp::Add;
        ExprPtr lhs;
        ExprPtr rhs;
    };
    // 赋值，如 a = b / a += b（右结合）。
    struct Assign : Expr {
        AssignOp op = AssignOp::Assign;
        ExprPtr lhs;
        ExprPtr rhs;
    };
    // 类型转换，如 x as i32。
    struct Cast : Expr {
        ExprPtr operand;
        TypePtr target;
    };
    // 函数调用，如 f(x, y)。
    struct Call : Expr {
        ExprPtr target;
        std::vector<ExprPtr> args;
    };
    // 方法调用，如 x.m(args) / a.foo::<T>()。
    struct MethodCall : Expr {
        ExprPtr target;
        PathSegment method;
        std::vector<ExprPtr> args;
    };
    // 字段访问，如 self.name。
    struct Field : Expr {
        ExprPtr target;
        std::string name;
    };
    // 下标访问，如 a[i]。
    struct Index : Expr {
        ExprPtr target;
        ExprPtr index;
    };
    // 结构体字面量里的一个字段初始化，如 x: 1。
    struct StructFieldInit {
        SourceLoc loc;
        std::string name;
        ExprPtr value;
    };
    // 结构体字面量，如 S { x: 1, y: 2 }。
    struct StructLit : Expr {
        std::vector<PathSegment> path;
        std::vector<StructFieldInit> fields;
    };
    // 数组字面量，如 [1, 2, 3]。
    struct ArrayLit : Expr {
        std::vector<ExprPtr> elements;
    };
    // 重复数组，如 [0; 4]（element 重复 count 次）。
    struct ArrayRepeat : Expr {
        ExprPtr element;
        ExprPtr count;
    };
    // if 表达式：condition、thenBlock、可选 else 分支（可能是块或另一个 if）。
    struct If : Expr {
        ExprPtr condition;
        BlockPtr thenBlock;
        ExprPtr elseBranch;
    };
    // 无限循环：loop { body }。
    struct Loop : Expr {
        BlockPtr body;
    };
    // while 循环：while condition { body }。
    struct While : Expr {
        ExprPtr condition;
        BlockPtr body;
    };
    // break：break; 或 break value;（value 可空）。
    struct Break : Expr {
        ExprPtr value;
    };
    // return：return; 或 return value;（value 可空）。
    struct Return : Expr {
        ExprPtr value;
    };
    // continue。
    struct Continue : Expr {};

    // ---------------- 泛型实参 ----------------
    // 泛型实参基类。
    struct GenericArg : Node {};
    // 生命周期实参，如 'a。
    struct LifetimeArg : GenericArg {
        Lifetime value;
    };
    // 类型实参，如 i32 / Box<Node>。
    struct TypeArg : GenericArg {
        TypePtr value;
    };

    // 编译单元（一个源文件）的根节点。
    struct Crate : Node {
        std::vector<ItemPtr> items;
    };


}
