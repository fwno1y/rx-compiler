#pragma once

#include <memory>
#include <vector>

#include "antlr4-runtime.h"
#include "RxParser.h"
#include "ast/AST.h"

namespace rx {

    class AstBuilder {
    public:
        explicit AstBuilder(RxParser& parser) : parser(parser) {}

        ast::Crate buildCrate(RxParser::CrateContext* ctx);

    private:
        RxParser& parser;

        ast::SourceLoc loc(antlr4::ParserRuleContext* ctx);

        // Item 类
        ast::ItemPtr buildItem(RxParser::ItemContext* ctx);
        std::shared_ptr<ast::UseDecl> buildUseDecl(RxParser::UseDeclarationContext* ctx);
        ast::UseTreePtr buildUseTree(RxParser::UseTreeContext* ctx);
        std::vector<ast::IdentSegment> buildUsePath(RxParser::UsePathContext* ctx);
        std::shared_ptr<ast::FunctionDef> buildFunctionDef(RxParser::FunctionDefinitionContext* ctx);
        void buildFunctionParameters(RxParser::FunctionParametersContext* ctx, ast::FunctionDef& fn);
        std::shared_ptr<ast::StructDef> buildStructDef(RxParser::StructDefinitionContext* ctx);
        std::shared_ptr<ast::ConstItem> buildConstItem(RxParser::ConstantItemContext* ctx);
        std::shared_ptr<ast::Impl> buildImpl(RxParser::InherentImplContext* ctx);

        // 泛型、生命周期
        ast::GenericParams buildGenericParams(RxParser::GenericParamsContext* ctx);
        ast::GenericArgs buildGenericArgs(RxParser::GenericArgsContext* ctx);
        ast::Lifetime buildLifetime(RxParser::LifetimeContext* ctx);
        ast::LifetimeBounds buildLifetimeBounds(RxParser::LifetimeBoundsContext* ctx);
        ast::WhereClause buildWhereClause(RxParser::WhereClauseContext* ctx);

        // Type 类
        ast::TypePtr buildType(RxParser::TypeRefContext* ctx);
        ast::TypePtr buildReferenceType(RxParser::ReferenceTypeContext* ctx);
        ast::TypePtr buildArrayType(RxParser::ArrayTypeContext* ctx);
        ast::TypePtr buildTypePath(RxParser::TypePathContext* ctx);
        ast::TypePtr buildClosedCastType(RxParser::ClosedCastTypeContext* ctx);
        ast::IdentSegment buildPathIdent(RxParser::PathIdentSegmentContext* ctx);
        ast::PathSegment buildPathExprSegment(RxParser::PathExprSegmentContext* ctx);
        std::vector<ast::PathSegment> buildPathInExpression(RxParser::PathInExpressionContext* ctx);

        // 常量值
        ast::ExprPtr buildConstValue(RxParser::ConstValueContext* ctx);
        ast::ExprPtr buildMagnitude(RxParser::MagnitudeContext* ctx);

        // Stmt 类
        ast::StmtPtr buildStmt(RxParser::StatementContext* ctx);
        std::shared_ptr<ast::LetStmt> buildLet(RxParser::LetStatementContext* ctx);
        ast::BlockPtr buildBlock(RxParser::BlockExpressionContext* ctx);

        // Expr 类
        ast::ExprPtr buildExpr(antlr4::ParserRuleContext* ctx);
        ast::ExprPtr buildExprWithBlock(antlr4::ParserRuleContext* ctx);
        ast::ExprPtr buildIf(RxParser::IfExpressionContext* ctx);
        ast::ExprPtr buildPrimary(antlr4::ParserRuleContext* ctx);
        ast::ExprPtr buildNonBlockPrimary(RxParser::NonBlockPrimaryContext* ctx);
        ast::ExprPtr buildConditionPrimary(RxParser::ConditionPrimaryWithoutBareBlockContext* ctx);
        ast::ExprPtr buildLiteral(RxParser::LiteralExpressionContext* ctx);
        ast::ExprPtr buildArray(RxParser::ArrayExpressionContext* ctx);
        ast::ExprPtr buildPostfix(antlr4::ParserRuleContext* ctx);
        ast::ExprPtr applyPostfix(ast::ExprPtr base, antlr4::ParserRuleContext* suffix);
        ast::ExprPtr applyDot(ast::ExprPtr base, RxParser::DotSuffixContext* ctx);
        ast::ExprPtr buildUnary(antlr4::ParserRuleContext* ctx);
        ast::ExprPtr buildCast(antlr4::ParserRuleContext* ctx);
        ast::ExprPtr buildAssign(antlr4::ParserRuleContext* ctx);
        ast::ExprPtr buildFold(antlr4::ParserRuleContext* ctx);
    };

}
