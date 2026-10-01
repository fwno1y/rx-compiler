#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "SourceLoc.h"

namespace rx::ast {
    struct Item;
    struct Type;
    struct Expr;
    struct Stmt;
    struct UseTree;
    struct WhereClauseItem;
    struct GenericArg;
    struct Block;
    using TypePtr = std::unique_ptr<Type>;
    using ExprPtr = std::unique_ptr<Expr>;
    using StmtPtr = std::unique_ptr<Stmt>;
    using ItemPtr = std::unique_ptr<Item>;
    using UseTreePtr       = std::unique_ptr<UseTree>;
    using WhereClauseItemPtr = std::unique_ptr<WhereClauseItem>;
    using GenericArgPtr    = std::unique_ptr<GenericArg>;
    using BlockPtr         = std::unique_ptr<Block>;

    // 标识
    enum class IdentifierKind {
        Normal,
        SelfValue,
        SelfType,
        Super,
        Crate,
    };
    // 字面量
    enum class LiteralKind {
        Integer,
        Bool,
    };
    //一元运算符
    enum class UnaryOp {
        Neg, // -
        Not, // !
        DeRef, // *
        Ref, // &
        RefMut, // &mut
        RefRef, // &&
        RefRefMut, // &&mut
    };
    enum class BinaryOp {
        Add, Sub, Mul, Div, Rem, // +,-,*,/,%
        Eq, Ne, Lt, Le, Gt, Ge, // ==,!=,<,<=,>,>=
        And, Or, // &&,||
        BitAnd, BitOr, BitXor, // &,|,^
        Shl, Shr // <<,>>
    };
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

    struct Node {
        SourceLoc loc;
        virtual ~Node() = default;
    };

    // [修改 2] IdentSegment 从原来“路径类”位置提前到这里：
    //          use 路径同样需要它，并且它携带 self/super/crate 的 kind。
    struct IdentSegment {
        SourceLoc loc;
        IdentifierKind kind = IdentifierKind::Normal;
        std::string text;
    };

    // Use 的树
    struct UseTree : Node {};
    struct UseTreePath : UseTree {
        // [修改 3] path 由 std::vector<std::string> 改为 IdentSegment；
        //          并增加 aliasUnderscore 表示 `use a as _;`
        std::vector<IdentSegment> path;
        bool hasAlias = false;
        std::string alias;
        bool aliasUnderscore = false;
    };
    struct UseTreeGlobal : UseTree {
        // [修改 4] 增加 hasPrefix，区分“无前缀”与 `use ::*;`
        bool hasPrefix = false;
        std::vector<IdentSegment> prefix;   // [修改 2] 同步改为 IdentSegment
    };
    struct UseTreeGroup : UseTree {
        bool hasPrefix = false;             // [修改 4]
        std::vector<IdentSegment> prefix;   // [修改 2]
        std::vector<UseTreePtr> items;
    };

    // 泛型、生命周期
    struct Lifetime {
        SourceLoc loc;
        std::string name;
    };
    struct LifetimeBounds {
        SourceLoc loc;
        std::vector<Lifetime> lifetimes;
    };
    struct LifetimeParam {
        SourceLoc loc;
        Lifetime lifetime;
        std::optional<LifetimeBounds> bounds;
    };
    struct GenericParams {
        SourceLoc loc;
        std::vector<LifetimeParam> lifetimes;
    };
    struct TypeParamBounds {
        SourceLoc loc;
        std::vector<Lifetime> lifetimes;
    };
    struct WhereClauseItem : Node {};
    struct LifetimeWhereItem : WhereClauseItem {
        Lifetime lifetime;
        LifetimeBounds bounds;
    };
    struct TypeWhereItem : WhereClauseItem {
        TypePtr type;
        std::optional<TypeParamBounds> bounds;
    };
    struct WhereClause {
        SourceLoc loc;
        std::vector<WhereClauseItemPtr> items;
    };

    // 函数及其辅助
    struct Binding {
        SourceLoc loc;
        bool isMut = false;
        std::string name;
    };
    struct SelfParam {
        SourceLoc loc;
        bool isRef = false;
        bool isDoubleRef = false;
        bool isMut = false;
        std::optional<Lifetime> lifetime;
    };
    struct FunctionParam {
        SourceLoc loc;
        Binding binding;
        TypePtr type;
    };
    struct ParamList {
        SourceLoc loc;
        std::optional<SelfParam> selfParam;
        std::vector<FunctionParam> params;
    };

    // Item 类
    struct Item : Node {};
    struct UseDecl : Item {
        UseTreePtr tree;
    };
    struct Attribute {
        SourceLoc loc;
        std::vector<std::string> derives;
    };
    struct StructField {
        SourceLoc loc;
        std::string name;
        TypePtr type;
    };
    struct FunctionDef : Item {
        std::string name;
        std::optional<GenericParams> generics;
        ParamList params;
        TypePtr returnType;
        std::optional<WhereClause> where;
        BlockPtr body;
    };
    struct StructDef : Item {
        std::vector<Attribute> attributes;
        std::string name;
        std::optional<GenericParams> generics;
        std::optional<WhereClause> where;
        std::vector<StructField> fields;
    };
    struct ConstItem : Item {
        std::string name;
        TypePtr     type;
        ExprPtr     value;
    };
    struct Impl : Item {
        std::optional<GenericParams> generics;
        TypePtr target;
        std::optional<WhereClause> where;
        std::vector<ItemPtr> items;
    };

    // 路径类
    // [修改 2] IdentSegment 已上移，这里删除了原来的定义。
    // [修改 5] GenericArgs 删除多余的 ident 字段：
    //          文法 genericArgs 只是 `< 实参列表 >`，标识名属于 PathSegment。
    struct GenericArgs {
        SourceLoc loc;
        std::vector<GenericArgPtr> args;
    };
    struct PathSegment {
        SourceLoc loc;
        IdentSegment ident;
        std::optional<GenericArgs> generics;
    };

    // Type 类
    struct Type : Node {};
    struct TypePath : Type {
        std::vector<PathSegment> segments;
    };
    struct RefType : Type {
        bool isDouble = false;
        bool isMut = false;
        std::optional<Lifetime> lifetime;
        TypePtr target;
    };
    struct ArrayType : Type {
        TypePtr element;
        ExprPtr length;
    };
    struct UnitType : Type {};
    struct ParenType : Type {
        TypePtr inner;
    };

    // Stmt 类
    struct Stmt : Node {};
    struct LetStmt : Stmt {
        Binding binding;
        TypePtr type;
        ExprPtr init;
    };
    struct ExprStmt : Stmt {
        ExprPtr expr;
        bool hasSemi = false;  // 是否分号结尾
    };

    // Expr 类
    struct Expr : Node {};
    struct Block : Expr {
        std::vector<StmtPtr> stmts;
        ExprPtr tail;
    };
    struct Literal : Expr {
        LiteralKind kind = LiteralKind::Integer;
        std::string text;
    };
    // [修改 7] 新增：表示 `()` 这个单元表达式。
    struct UnitExpr : Expr {};
    struct Path : Expr {
        std::vector<PathSegment> segments;
    };
    struct Unary : Expr {
        UnaryOp op = UnaryOp::Neg;
        ExprPtr operand;
    };
    struct Binary : Expr {
        BinaryOp op = BinaryOp::Add;
        ExprPtr lhs;
        ExprPtr rhs;
    };
    struct Assign : Expr {
        AssignOp op = AssignOp::Assign;
        ExprPtr lhs;
        ExprPtr rhs;
    };
    struct Cast : Expr {
        ExprPtr operand;
        TypePtr target;
    };
    struct Call : Expr {
        ExprPtr target;
        std::vector<ExprPtr> args;
    };
    struct MethodCall : Expr {
        ExprPtr target;
        PathSegment method;
        std::vector<ExprPtr> args;
    };
    struct Field : Expr {
        ExprPtr target;
        std::string name;
    };
    struct Index : Expr {
        ExprPtr target;
        ExprPtr index;
    };
    // [修改 6] 新增结构体字面量的单个字段初始化节点。
    struct StructFieldInit {
        SourceLoc loc;
        std::string name;
        ExprPtr value;
    };
    struct StructLit : Expr {
        // [修改 6] 删除了这里重复声明的 `SourceLoc loc;`（它会遮蔽 Node::loc）。
        std::vector<PathSegment> path;
        std::vector<StructFieldInit> fields;   // [修改 6] 补上字段初始化列表。
    };
    struct ArrayLit : Expr {
        std::vector<ExprPtr> elements;
    };
    struct ArrayRepeat : Expr {
        ExprPtr element;
        ExprPtr count;
    };
    struct If : Expr {
        ExprPtr condition;
        BlockPtr thenBlock;
        ExprPtr elseBranch;   // [修改 8] 原 elseBlock；else 后面可能跟的是 if。
    };
    struct Loop : Expr {
        BlockPtr body;
    };
    struct While : Expr {
        ExprPtr condition;
        BlockPtr body;
    };
    struct Break : Expr {
        ExprPtr value;
    };
    struct Return : Expr {
        ExprPtr value;
    };
    struct Continue : Expr {};
    //泛型实参
    struct GenericArg : Node {};
    struct LifetimeArg : GenericArg {
        Lifetime value;
    };
    struct TypeArg : GenericArg {
        TypePtr value;
    };

    // 容器
    // [修改 1] Crate 从文件开头移到所有 Item 定义之后：
    //          原来 std::vector<ItemPtr> 在 Item 尚未定义时使用它，顺序反了。
    // 一个编译单元
    struct Crate {
        std::vector<ItemPtr> items;
    };


}
