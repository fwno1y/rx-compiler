#include "AstBuilder.h"

#include <utility>

using antlr4::ParserRuleContext;
using antlr4::tree::TerminalNode;
namespace ast = rx::ast;

namespace {

    ParserRuleContext* nthRuleChild(ParserRuleContext* ctx, size_t n) {
        size_t k = 0;
        for (auto* ch : ctx->children) {
            if (auto* rc = dynamic_cast<ParserRuleContext*>(ch)) {
                if (k == n) return rc;
                ++k;
            }
        }
        return nullptr;
    }

    bool binOpFromToken(size_t t, ast::BinaryOp& op) {
        switch (t) {
            case RxParser::PLUS:
                op = ast::BinaryOp::Add;
                return true;
            case RxParser::MINUS:
                op = ast::BinaryOp::Sub;
                return true;
            case RxParser::STAR:
                op = ast::BinaryOp::Mul;
                return true;
            case RxParser::SLASH:
                op = ast::BinaryOp::Div;
                return true;
            case RxParser::PERCENT:
                op = ast::BinaryOp::Rem;
                return true;
            case RxParser::AMP:
                op = ast::BinaryOp::BitAnd;
                return true;
            case RxParser::PIPE:
                op = ast::BinaryOp::BitOr;
                return true;
            case RxParser::CARET:
                op = ast::BinaryOp::BitXor;
                return true;
            case RxParser::SHL:
                op = ast::BinaryOp::Shl;
                return true;
            case RxParser::LT:
                op = ast::BinaryOp::Lt;
                return true;
            case RxParser::ANDAND:
                op = ast::BinaryOp::And;
                return true;
            case RxParser::OROR:
                op = ast::BinaryOp::Or;
                return true;
            default:
                return false;
        }
    }

    bool binOpFromRuleCtx(ParserRuleContext* rc, ast::BinaryOp& op) {
        switch (rc->getRuleIndex()) {
            case RxParser::RuleMultiplicativeOperator: {
                auto* c = static_cast<RxParser::MultiplicativeOperatorContext*>(rc);
                if (c->STAR()) {
                    op = ast::BinaryOp::Mul;
                } else if (c->SLASH()) {
                    op = ast::BinaryOp::Div;
                } else {
                    op = ast::BinaryOp::Rem;
                }
                return true;
            }
            case RxParser::RuleAdditiveOperator: {
                auto* c = static_cast<RxParser::AdditiveOperatorContext*>(rc);
                op = c->PLUS() ? ast::BinaryOp::Add : ast::BinaryOp::Sub;
                return true;
            }
            case RxParser::RuleShiftRight:
                op = ast::BinaryOp::Shr;
                return true;
            case RxParser::RuleComparisonExceptLt: {
                auto* c = static_cast<RxParser::ComparisonExceptLtContext*>(rc);
                if (c->EQEQ()) {
                    op = ast::BinaryOp::Eq;
                } else if (c->NE()) {
                    op = ast::BinaryOp::Ne;
                } else if (c->LE()) {
                    op = ast::BinaryOp::Le;
                } else if (c->GE_EQ() || c->SHR_EQ()) {
                    op = ast::BinaryOp::Ge;
                } else {
                    op = ast::BinaryOp::Gt;
                }
                return true;
            }
            default:
                return false;
        }
    }

    ast::UnaryOp mapUnaryOp(RxParser::UnaryOperatorContext* c) {
        if (c->MINUS()) return ast::UnaryOp::Neg;
        if (c->NOT()) return ast::UnaryOp::Not;
        if (c->STAR()) return ast::UnaryOp::DeRef;
        if (c->ANDAND()) return c->MUT() ? ast::UnaryOp::RefRefMut : ast::UnaryOp::RefRef;
        return c->MUT() ? ast::UnaryOp::RefMut : ast::UnaryOp::Ref;
    }

    ast::AssignOp mapAssignOp(RxParser::AssignmentOperatorContext* c) {
        if (c->equalsSign()) return ast::AssignOp::Assign;
        if (c->PLUS_ASSIGN()) return ast::AssignOp::AddAssign;
        if (c->MINUS_ASSIGN()) return ast::AssignOp::SubAssign;
        if (c->STAR_ASSIGN()) return ast::AssignOp::MulAssign;
        if (c->SLASH_ASSIGN()) return ast::AssignOp::DivAssign;
        if (c->PERCENT_ASSIGN()) return ast::AssignOp::RemAssign;
        if (c->AMP_ASSIGN()) return ast::AssignOp::BitAndAssign;
        if (c->PIPE_ASSIGN()) return ast::AssignOp::BitOrAssign;
        if (c->CARET_ASSIGN()) return ast::AssignOp::BitXorAssign;
        if (c->SHL_ASSIGN()) return ast::AssignOp::ShlAssign;
        if (c->SHR_EQ()) return ast::AssignOp::ShrAssign;
        return ast::AssignOp::Assign;
    }

} // namespace

namespace rx {

    ast::SourceLoc AstBuilder::loc(ParserRuleContext* ctx) {
        auto* t = ctx->getStart();
        return ast::SourceLoc{ static_cast<std::size_t>(t->getLine()), static_cast<std::size_t>(t->getCharPositionInLine()) };
    }

    // Item 类

    ast::Crate AstBuilder::buildCrate(RxParser::CrateContext* ctx) {
        ast::Crate crate;
        for (auto* it : ctx->item()) crate.items.push_back(buildItem(it));
        return crate;
    }

    ast::ItemPtr AstBuilder::buildItem(RxParser::ItemContext* ctx) {
        if (auto* u = ctx->useDeclaration()) return buildUseDecl(u);
        if (auto* f = ctx->functionDefinition()) return buildFunctionDef(f);
        if (auto* s = ctx->structDefinition()) return buildStructDef(s);
        if (auto* c = ctx->constantItem()) return buildConstItem(c);
        if (auto* i = ctx->inherentImpl()) return buildImpl(i);
        return nullptr;
    }

    std::shared_ptr<ast::UseDecl> AstBuilder::buildUseDecl(RxParser::UseDeclarationContext* ctx) {
        auto n = std::make_shared<ast::UseDecl>();
        n->loc = loc(ctx);
        n->tree = buildUseTree(ctx->useTree());
        return n;
    }

    ast::UseTreePtr AstBuilder::buildUseTree(RxParser::UseTreeContext* ctx) {
        if (ctx->STAR() || ctx->LBRACE()) {
            std::vector<ast::IdentSegment> prefix;
            bool hasPrefix = false;
            if (ctx->usePath()) {
                prefix = buildUsePath(ctx->usePath());
                hasPrefix = true;
            }

            if (ctx->STAR()) {
                auto n = std::make_shared<ast::UseTreeGlobal>();
                n->loc = loc(ctx);
                n->hasPrefix = hasPrefix;
                n->prefix = std::move(prefix);
                return n;
            }
            auto n = std::make_shared<ast::UseTreeGroup>();
            n->loc = loc(ctx);
            n->hasPrefix = hasPrefix;
            n->prefix = std::move(prefix);
            for (auto* t : ctx->useTree()) n->items.push_back(buildUseTree(t));
            return n;
        }

        auto n = std::make_shared<ast::UseTreePath>();
        n->loc = loc(ctx);
        n->path = buildUsePath(ctx->usePath());
        if (ctx->AS()) {
            if (ctx->UNDERSCORE()) {
                n->aliasUnderscore = true;
            } else {
                n->hasAlias = true;
                n->alias = ctx->identifier()->getText();
            }
        }
        return n;
    }

    std::vector<ast::IdentSegment> AstBuilder::buildUsePath(RxParser::UsePathContext* ctx) {
        std::vector<ast::IdentSegment> segs;
        for (auto* s : ctx->usePathSegment()) {
            ast::IdentSegment seg;
            seg.loc = loc(s);
            if (auto* id = s->identifier()) {
                seg.kind = ast::IdentifierKind::Normal;
                seg.text = id->getText();
            } else if (s->SELF_VALUE()) {
                seg.kind = ast::IdentifierKind::SelfValue;
                seg.text = "self";
            } else if (s->SUPER()) {
                seg.kind = ast::IdentifierKind::Super;
                seg.text = "super";
            } else {
                seg.kind = ast::IdentifierKind::Crate;
                seg.text = "crate";
            }
            segs.push_back(std::move(seg));
        }
        return segs;
    }

    std::shared_ptr<ast::FunctionDef> AstBuilder::buildFunctionDef(RxParser::FunctionDefinitionContext* ctx) {
        auto n = std::make_shared<ast::FunctionDef>();
        n->loc = loc(ctx);
        n->name = ctx->identifier()->getText();
        if (ctx->genericParams()) n->generics = buildGenericParams(ctx->genericParams());
        if (ctx->functionParameters()) buildFunctionParameters(ctx->functionParameters(), *n);
        if (ctx->typeRef()) n->returnType = buildType(ctx->typeRef());
        if (ctx->whereClause()) n->where = buildWhereClause(ctx->whereClause());
        n->body = buildBlock(ctx->blockExpression());
        return n;
    }

    void AstBuilder::buildFunctionParameters(RxParser::FunctionParametersContext* ctx, ast::FunctionDef& fn) {
        fn.params.loc = loc(ctx);
        if (auto* sp = ctx->selfParam()) {
            ast::SelfParam s;
            s.loc = loc(sp);
            s.isRef = sp->AMP() != nullptr;
            s.isDoubleRef = false;
            s.isMut = sp->MUT() != nullptr;
            if (sp->lifetime()) s.lifetime = buildLifetime(sp->lifetime());
            fn.params.selfParam = std::move(s);
        }
        for (auto* p : ctx->functionParam()) {
            ast::FunctionParam fp;
            fp.loc = loc(p);
            auto* b = p->identifierBinding();
            fp.binding.loc = loc(b);
            fp.binding.isMut = b->MUT() != nullptr;
            fp.binding.name = b->identifier()->getText();
            fp.type = buildType(p->typeRef());
            fn.params.params.push_back(std::move(fp));
        }
    }

    std::shared_ptr<ast::StructDef> AstBuilder::buildStructDef(RxParser::StructDefinitionContext* ctx) {
        auto n = std::make_shared<ast::StructDef>();
        n->loc = loc(ctx);
        for (auto* attr : ctx->outerAttribute()) {
            ast::Attribute a;
            a.loc = loc(attr);
            for (auto* d : attr->deriveName()) {
                if (d->COPY()) {
                    a.derives.emplace_back("Copy");
                } else if (d->CLONE()) {
                    a.derives.emplace_back("Clone");
                } else if (d->PARTIAL_EQ()) {
                    a.derives.emplace_back("PartialEq");
                } else if (d->EQ()) {
                    a.derives.emplace_back("Eq");
                }
            }
            n->attributes.push_back(std::move(a));
        }
        n->name = ctx->identifier()->getText();
        if (ctx->genericParams()) n->generics = buildGenericParams(ctx->genericParams());
        if (ctx->whereClause()) n->where = buildWhereClause(ctx->whereClause());
        for (auto* f : ctx->structField()) {
            ast::StructField sf;
            sf.loc = loc(f);
            sf.name = f->identifier()->getText();
            sf.type = buildType(f->typeRef());
            n->fields.push_back(std::move(sf));
        }
        return n;
    }

    std::shared_ptr<ast::ConstItem> AstBuilder::buildConstItem(RxParser::ConstantItemContext* ctx) {
        auto n = std::make_shared<ast::ConstItem>();
        n->loc = loc(ctx);
        n->name = ctx->identifier()->getText();
        n->type = buildType(ctx->typeRef());
        n->value = buildConstValue(ctx->constValue());
        return n;
    }

    std::shared_ptr<ast::Impl> AstBuilder::buildImpl(RxParser::InherentImplContext* ctx) {
        auto n = std::make_shared<ast::Impl>();
        n->loc = loc(ctx);
        if (ctx->genericParams()) n->generics = buildGenericParams(ctx->genericParams());
        n->target = buildType(ctx->typeRef());
        if (ctx->whereClause()) n->where = buildWhereClause(ctx->whereClause());
        for (auto* a : ctx->associatedItem()) {
            if (auto* c = a->constantItem()) {
                n->items.push_back(buildConstItem(c));
            } else if (auto* f = a->functionDefinition()) {
                n->items.push_back(buildFunctionDef(f));
            }
        }
        return n;
    }

    // 泛型、生命周期

    ast::Lifetime AstBuilder::buildLifetime(RxParser::LifetimeContext* ctx) {
        return ast::Lifetime{ loc(ctx), ctx->LIFETIME()->getText() };
    }

    ast::LifetimeBounds AstBuilder::buildLifetimeBounds(RxParser::LifetimeBoundsContext* ctx) {
        ast::LifetimeBounds b;
        b.loc = loc(ctx);
        for (auto* l : ctx->lifetime()) b.lifetimes.push_back(buildLifetime(l));
        return b;
    }

    ast::GenericParams AstBuilder::buildGenericParams(RxParser::GenericParamsContext* ctx) {
        ast::GenericParams gp;
        gp.loc = loc(ctx);
        for (auto* lp : ctx->lifetimeParam()) {
            ast::LifetimeParam p;
            p.loc = loc(lp);
            p.lifetime = buildLifetime(lp->lifetime());
            if (lp->lifetimeBounds()) p.bounds = buildLifetimeBounds(lp->lifetimeBounds());
            gp.lifetimes.push_back(std::move(p));
        }
        return gp;
    }

    ast::GenericArgs AstBuilder::buildGenericArgs(RxParser::GenericArgsContext* ctx) {
        ast::GenericArgs ga;
        ga.loc = loc(ctx);
        for (auto* a : ctx->genericArg()) {
            if (a->lifetime()) {
                auto la = std::make_shared<ast::LifetimeArg>();
                la->loc = loc(a);
                la->value = buildLifetime(a->lifetime());
                ga.args.push_back(std::move(la));
            } else {
                auto ta = std::make_shared<ast::TypeArg>();
                ta->loc = loc(a);
                ta->value = buildType(a->typeRef());
                ga.args.push_back(std::move(ta));
            }
        }
        return ga;
    }

    ast::WhereClause AstBuilder::buildWhereClause(RxParser::WhereClauseContext* ctx) {
        ast::WhereClause wc;
        wc.loc = loc(ctx);
        for (auto* item : ctx->whereClauseItem()) {
            if (item->lifetime()) {
                auto it = std::make_shared<ast::LifetimeWhereItem>();
                it->loc = loc(item);
                it->lifetime = buildLifetime(item->lifetime());
                if (item->lifetimeBounds()) it->bounds = buildLifetimeBounds(item->lifetimeBounds());
                wc.items.push_back(std::move(it));
            } else {
                auto it = std::make_shared<ast::TypeWhereItem>();
                it->loc = loc(item);
                it->type = buildType(item->typeRef());
                if (item->typeParamBounds()) {
                    ast::TypeParamBounds tb;
                    tb.loc = loc(item->typeParamBounds());
                    for (auto* l : item->typeParamBounds()->lifetime()) tb.lifetimes.push_back(buildLifetime(l));
                    it->bounds = std::move(tb);
                }
                wc.items.push_back(std::move(it));
            }
        }
        return wc;
    }

    // Type 类

    ast::TypePtr AstBuilder::buildType(RxParser::TypeRefContext* ctx) {
        if (auto* tp = ctx->typePath()) return buildTypePath(tp);
        if (auto* rt = ctx->referenceType()) return buildReferenceType(rt);
        if (auto* at = ctx->arrayType()) return buildArrayType(at);
        if (ctx->LPAREN()) {
            if (auto* inner = ctx->typeRef()) {
                auto n = std::make_shared<ast::ParenType>();
                n->loc = loc(ctx);
                n->inner = buildType(inner);
                return n;
            }
            auto n = std::make_shared<ast::UnitType>();
            n->loc = loc(ctx);
            return n;
        }
        return nullptr;
    }

    ast::TypePtr AstBuilder::buildReferenceType(RxParser::ReferenceTypeContext* ctx) {
        auto n = std::make_shared<ast::RefType>();
        n->loc = loc(ctx);
        n->isDouble = ctx->ANDAND() != nullptr;
        n->isMut = ctx->MUT() != nullptr;
        if (ctx->lifetime()) n->lifetime = buildLifetime(ctx->lifetime());
        n->target = buildType(ctx->typeRef());
        return n;
    }

    ast::TypePtr AstBuilder::buildArrayType(RxParser::ArrayTypeContext* ctx) {
        auto n = std::make_shared<ast::ArrayType>();
        n->loc = loc(ctx);
        n->element = buildType(ctx->typeRef());
        n->length = buildConstValue(ctx->constValue());
        return n;
    }

    ast::TypePtr AstBuilder::buildTypePath(RxParser::TypePathContext* ctx) {
        auto n = std::make_shared<ast::TypePath>();
        n->loc = loc(ctx);
        for (auto* s : ctx->typePathSegment()) {
            ast::PathSegment seg;
            seg.loc = loc(s);
            seg.ident = buildPathIdent(s->pathIdentSegment());
            if (s->genericArgs()) seg.generics = buildGenericArgs(s->genericArgs());
            n->segments.push_back(std::move(seg));
        }
        return n;
    }

    ast::TypePtr AstBuilder::buildClosedCastType(RxParser::ClosedCastTypeContext* ctx) {
        if (ctx->LPAREN()) {
            if (auto* inner = ctx->typeRef()) {
                auto n = std::make_shared<ast::ParenType>();
                n->loc = loc(ctx);
                n->inner = buildType(inner);
                return n;
            }
            auto n = std::make_shared<ast::UnitType>();
            n->loc = loc(ctx);
            return n;
        }
        if (auto* at = ctx->arrayType()) return buildArrayType(at);
        if (ctx->AMP() || ctx->ANDAND()) {
            auto n = std::make_shared<ast::RefType>();
            n->loc = loc(ctx);
            n->isDouble = ctx->ANDAND() != nullptr;
            n->isMut = ctx->MUT() != nullptr;
            if (ctx->lifetime()) n->lifetime = buildLifetime(ctx->lifetime());
            n->target = buildClosedCastType(ctx->closedCastType());
            return n;
        }
        auto tp = std::make_shared<ast::TypePath>();
        tp->loc = loc(ctx);
        for (auto* s : ctx->typePathSegment()) {
            ast::PathSegment seg;
            seg.loc = loc(s);
            seg.ident = buildPathIdent(s->pathIdentSegment());
            if (s->genericArgs()) seg.generics = buildGenericArgs(s->genericArgs());
            tp->segments.push_back(std::move(seg));
        }
        ast::PathSegment last;
        last.loc = loc(ctx->pathIdentSegment());
        last.ident = buildPathIdent(ctx->pathIdentSegment());
        if (ctx->genericArgs()) last.generics = buildGenericArgs(ctx->genericArgs());
        tp->segments.push_back(std::move(last));
        return tp;
    }

    ast::IdentSegment AstBuilder::buildPathIdent(RxParser::PathIdentSegmentContext* ctx) {
        ast::IdentSegment s;
        s.loc = loc(ctx);
        if (auto* id = ctx->identifier()) {
            s.kind = ast::IdentifierKind::Normal;
            s.text = id->getText();
        } else if (ctx->SELF_VALUE()) {
            s.kind = ast::IdentifierKind::SelfValue;
            s.text = "self";
        } else {
            s.kind = ast::IdentifierKind::SelfType;
            s.text = "Self";
        }
        return s;
    }

    ast::PathSegment AstBuilder::buildPathExprSegment(RxParser::PathExprSegmentContext* ctx) {
        ast::PathSegment seg;
        seg.loc = loc(ctx);
        seg.ident = buildPathIdent(ctx->pathIdentSegment());
        if (ctx->genericArgs()) seg.generics = buildGenericArgs(ctx->genericArgs());
        return seg;
    }

    std::vector<ast::PathSegment> AstBuilder::buildPathInExpression(RxParser::PathInExpressionContext* ctx) {
        std::vector<ast::PathSegment> segs;
        for (auto* s : ctx->pathExprSegment()) segs.push_back(buildPathExprSegment(s));
        return segs;
    }

    // 常量值

    ast::ExprPtr AstBuilder::buildConstValue(RxParser::ConstValueContext* ctx) {
        if (ctx->INTEGER_LITERAL() || ctx->TRUE() || ctx->FALSE()) {
            auto n = std::make_shared<ast::Literal>();
            n->loc = loc(ctx);
            if (ctx->INTEGER_LITERAL()) {
                n->kind = ast::LiteralKind::Integer;
                n->text = ctx->INTEGER_LITERAL()->getText();
            } else {
                n->kind = ast::LiteralKind::Bool;
                n->text = ctx->TRUE() ? "true" : "false";
            }
            return n;
        }
        if (auto* p = ctx->pathInExpression()) {
            auto n = std::make_shared<ast::Path>();
            n->loc = loc(ctx);
            n->segments = buildPathInExpression(p);
            return n;
        }
        if (ctx->MINUS()) {
            auto n = std::make_shared<ast::Unary>();
            n->loc = loc(ctx);
            n->op = ast::UnaryOp::Neg;
            n->operand = buildMagnitude(ctx->magnitude());
            return n;
        }
        if (ctx->constValue()) return buildConstValue(ctx->constValue());
        return nullptr;
    }

    ast::ExprPtr AstBuilder::buildMagnitude(RxParser::MagnitudeContext* ctx) {
        if (ctx->INTEGER_LITERAL()) {
            auto n = std::make_shared<ast::Literal>();
            n->loc = loc(ctx);
            n->kind = ast::LiteralKind::Integer;
            n->text = ctx->INTEGER_LITERAL()->getText();
            return n;
        }
        if (auto* p = ctx->pathInExpression()) {
            auto n = std::make_shared<ast::Path>();
            n->loc = loc(ctx);
            n->segments = buildPathInExpression(p);
            return n;
        }
        return buildMagnitude(ctx->magnitude());
    }

    // Stmt 类

    ast::StmtPtr AstBuilder::buildStmt(RxParser::StatementContext* ctx) {
        if (auto* l = ctx->letStatement()) return buildLet(l);
        if (auto* w = ctx->expressionWithBlock()) {
            auto n = std::make_shared<ast::ExprStmt>();
            n->loc = loc(ctx);
            n->expr = buildExprWithBlock(w);
            n->hasSemi = ctx->SEMI() != nullptr;
            return n;
        }
        if (auto* e = ctx->statementExpression()) {
            auto n = std::make_shared<ast::ExprStmt>();
            n->loc = loc(ctx);
            n->expr = buildExpr(e);
            n->hasSemi = true;
            return n;
        }
        return nullptr; // 单独的 ';'
    }

    std::shared_ptr<ast::LetStmt> AstBuilder::buildLet(RxParser::LetStatementContext* ctx) {
        auto n = std::make_shared<ast::LetStmt>();
        n->loc = loc(ctx);
        auto* b = ctx->identifierBinding();
        n->binding.loc = loc(b);
        n->binding.isMut = b->MUT() != nullptr;
        n->binding.name = b->identifier()->getText();
        if (ctx->typeRef()) n->type = buildType(ctx->typeRef());
        n->init = buildExpr(ctx->expression());
        return n;
    }

    ast::BlockPtr AstBuilder::buildBlock(RxParser::BlockExpressionContext* ctx) {
        auto n = std::make_shared<ast::Block>();
        n->loc = loc(ctx);
        for (auto* s : ctx->statement()) {
            auto st = buildStmt(s);
            if (st) n->stmts.push_back(std::move(st));
        }
        if (auto* t = ctx->statementExpression()) n->tail = buildExpr(t);
        return n;
    }

    // Expr 类

    ast::ExprPtr AstBuilder::buildExpr(ParserRuleContext* ctx) {
        if (!ctx) return nullptr;
        switch (ctx->getRuleIndex()) {
            // 透明包装
            case RxParser::RuleExpression:
            case RxParser::RuleConditionExpression:
            case RxParser::RuleConditionBreakExpression:
            case RxParser::RuleStatementExpression:
                return buildExpr(nthRuleChild(ctx, 0));

            // 赋值（右结合）
            case RxParser::RuleAssignmentExpression:
            case RxParser::RuleConditionAssignmentExpression:
            case RxParser::RuleConditionBreakAssignmentExpression:
            case RxParser::RuleStatementAssignmentExpression:
                return buildAssign(ctx);

            // 类型转换
            case RxParser::RuleCastExpression:
            case RxParser::RuleConditionCastExpression:
            case RxParser::RuleConditionBreakCastExpression:
            case RxParser::RuleStatementCastExpression:
            case RxParser::RuleClosedCastExpression:
            case RxParser::RuleConditionClosedCastExpression:
            case RxParser::RuleConditionBreakClosedCastExpression:
            case RxParser::RuleStatementClosedCastExpression:
                return buildCast(ctx);

            // 一元
            case RxParser::RuleUnaryExpression:
            case RxParser::RuleConditionUnaryExpression:
            case RxParser::RuleConditionBreakUnaryExpression:
            case RxParser::RuleStatementUnaryExpression:
                return buildUnary(ctx);

            // 后缀
            case RxParser::RulePostfixExpression:
            case RxParser::RuleConditionPostfixExpression:
            case RxParser::RuleConditionBreakPostfixExpression:
            case RxParser::RuleStatementPostfixExpression:
                return buildPostfix(ctx);

            // primary / literal
            case RxParser::RulePrimaryExpression:
            case RxParser::RuleConditionPrimary:
            case RxParser::RuleConditionPrimaryWithoutBareBlock:
            case RxParser::RuleNonBlockPrimary:
                return buildPrimary(ctx);
            case RxParser::RuleLiteralExpression:
                return buildLiteral(static_cast<RxParser::LiteralExpressionContext*>(ctx));

            // 带块的表达式
            case RxParser::RuleBlockExpression:
            case RxParser::RuleExpressionWithBlock:
            case RxParser::RuleIfExpression:
                return buildExprWithBlock(ctx);

            // 其余全是二元优先级链，通用左折叠
            default:
                return buildFold(ctx);
        }
    }

    ast::ExprPtr AstBuilder::buildFold(ParserRuleContext* ctx) {
        ast::ExprPtr acc;
        ast::BinaryOp pending = ast::BinaryOp::Add;
        for (auto* ch : ctx->children) {
            if (auto* tn = dynamic_cast<TerminalNode*>(ch)) {
                ast::BinaryOp op;
                if (binOpFromToken(tn->getSymbol()->getType(), op)) pending = op;
                continue;
            }
            auto* rc = dynamic_cast<ParserRuleContext*>(ch);
            if (!rc) continue;
            ast::BinaryOp op;
            if (binOpFromRuleCtx(rc, op)) {
                pending = op;
                continue;
            }

            auto operand = buildExpr(rc);
            if (!acc) {
                acc = std::move(operand);
                continue;
            }
            auto bin = std::make_shared<ast::Binary>();
            bin->loc = loc(ctx);
            bin->op = pending;
            bin->lhs = std::move(acc);
            bin->rhs = std::move(operand);
            acc = std::move(bin);
        }
        return acc;
    }

    ast::ExprPtr AstBuilder::buildAssign(ParserRuleContext* ctx) {
        ParserRuleContext* lhsCtx = nullptr;
        ParserRuleContext* opCtx = nullptr;
        ParserRuleContext* rhsCtx = nullptr;
        for (auto* ch : ctx->children) {
            auto* rc = dynamic_cast<ParserRuleContext*>(ch);
            if (!rc) continue;
            if (rc->getRuleIndex() == RxParser::RuleAssignmentOperator) opCtx = rc;
            else if (!lhsCtx) lhsCtx = rc;
            else if (!rhsCtx) rhsCtx = rc;
        }
        auto lhs = buildExpr(lhsCtx);
        if (!opCtx) return lhs;
        auto n = std::make_shared<ast::Assign>();
        n->loc = loc(ctx);
        n->op = mapAssignOp(static_cast<RxParser::AssignmentOperatorContext*>(opCtx));
        n->lhs = std::move(lhs);
        n->rhs = buildExpr(rhsCtx);
        return n;
    }

    ast::ExprPtr AstBuilder::buildCast(ParserRuleContext* ctx) {
        ast::ExprPtr acc;
        for (size_t i = 0; i < ctx->children.size(); ++i) {
            auto* child = ctx->children[i];
            if (auto* tn = dynamic_cast<TerminalNode*>(child)) {
                if (tn->getSymbol()->getType() == RxParser::AS) {
                    auto* rc = dynamic_cast<ParserRuleContext*>(ctx->children[i + 1]);
                    ast::TypePtr t;
                    if (rc->getRuleIndex() == RxParser::RuleClosedCastType) {
                        t = buildClosedCastType(static_cast<RxParser::ClosedCastTypeContext*>(rc));
                    } else {
                        t = buildType(static_cast<RxParser::TypeRefContext*>(rc));
                    }
                    auto cast = std::make_shared<ast::Cast>();
                    cast->loc = loc(ctx);
                    cast->operand = std::move(acc);
                    cast->target = std::move(t);
                    acc = std::move(cast);
                    ++i;
                }
                continue;
            }
            auto* rc = dynamic_cast<ParserRuleContext*>(child);
            if (rc && !acc) acc = buildExpr(rc);
        }
        return acc;
    }

    ast::ExprPtr AstBuilder::buildUnary(ParserRuleContext* ctx) {
        auto* first = nthRuleChild(ctx, 0);
        if (first && first->getRuleIndex() == RxParser::RuleUnaryOperator) {
            auto n = std::make_shared<ast::Unary>();
            n->loc = loc(ctx);
            n->op = mapUnaryOp(static_cast<RxParser::UnaryOperatorContext*>(first));
            n->operand = buildExpr(nthRuleChild(ctx, 1));
            return n;
        }
        return buildExpr(first);
    }

    ast::ExprPtr AstBuilder::buildPostfix(ParserRuleContext* ctx) {
        ast::ExprPtr base;
        for (auto* ch : ctx->children) {
            auto* rc = dynamic_cast<ParserRuleContext*>(ch);
            if (!rc) continue;
            if (!base) {
                if (rc->getRuleIndex() == RxParser::RuleExpressionWithBlock) {
                    base = buildExprWithBlock(rc);
                } else {
                    base = buildPrimary(rc);
                }
            } else {
                base = applyPostfix(std::move(base), rc);
            }
        }
        return base;
    }

    ast::ExprPtr AstBuilder::applyPostfix(ast::ExprPtr base, ParserRuleContext* rc) {
        if (rc->getRuleIndex() == RxParser::RuleDotSuffix)
            return applyDot(std::move(base), static_cast<RxParser::DotSuffixContext*>(rc));

        auto* c = static_cast<RxParser::PostfixSuffixContext*>(rc);
        if (auto* args = c->callArguments()) {
            auto n = std::make_shared<ast::Call>();
            n->loc = loc(rc);
            n->target = std::move(base);
            for (auto* e : args->expression()) n->args.push_back(buildExpr(e));
            return n;
        }
        if (c->LBRACKET()) {
            auto n = std::make_shared<ast::Index>();
            n->loc = loc(rc);
            n->target = std::move(base);
            n->index = buildExpr(c->expression());
            return n;
        }
        return applyDot(std::move(base), c->dotSuffix());
    }

    ast::ExprPtr AstBuilder::applyDot(ast::ExprPtr base, RxParser::DotSuffixContext* ctx) {
        if (auto* args = ctx->callArguments()) {
            auto n = std::make_shared<ast::MethodCall>();
            n->loc = loc(ctx);
            n->target = std::move(base);
            n->method = buildPathExprSegment(ctx->pathExprSegment());
            for (auto* e : args->expression()) n->args.push_back(buildExpr(e));
            return n;
        }
        auto n = std::make_shared<ast::Field>();
        n->loc = loc(ctx);
        n->target = std::move(base);
        n->name = ctx->identifier()->getText();
        return n;
    }

    ast::ExprPtr AstBuilder::buildPrimary(ParserRuleContext* ctx) {
        switch (ctx->getRuleIndex()) {
            case RxParser::RulePrimaryExpression: {
                auto* c = static_cast<RxParser::PrimaryExpressionContext*>(ctx);
                if (c->nonBlockPrimary()) return buildPrimary(c->nonBlockPrimary());
                return buildExprWithBlock(c->expressionWithBlock());
            }
            case RxParser::RuleConditionPrimary: {
                auto* c = static_cast<RxParser::ConditionPrimaryContext*>(ctx);
                if (c->conditionPrimaryWithoutBareBlock()) return buildPrimary(c->conditionPrimaryWithoutBareBlock());
                return buildExprWithBlock(c->blockExpression());
            }
            case RxParser::RuleNonBlockPrimary:
                return buildNonBlockPrimary(static_cast<RxParser::NonBlockPrimaryContext*>(ctx));
            case RxParser::RuleConditionPrimaryWithoutBareBlock:
                return buildConditionPrimary(static_cast<RxParser::ConditionPrimaryWithoutBareBlockContext*>(ctx));
        }
        return nullptr;
    }

    ast::ExprPtr AstBuilder::buildNonBlockPrimary(RxParser::NonBlockPrimaryContext* ctx) {
        if (auto* lit = ctx->literalExpression()) return buildLiteral(lit);
        if (auto* p = ctx->pathInExpression()) {
            auto segments = buildPathInExpression(p);
            if (ctx->LBRACE()) {
                auto n = std::make_shared<ast::StructLit>();
                n->loc = loc(ctx);
                n->path = std::move(segments);
                if (auto* fs = ctx->structExprFields()) {
                    for (auto* f : fs->structExprField()) {
                        ast::StructFieldInit fi;
                        fi.loc = loc(f);
                        fi.name = f->identifier()->getText();
                        fi.value = buildExpr(f->expression());
                        n->fields.push_back(std::move(fi));
                    }
                }
                return n;
            }
            auto n = std::make_shared<ast::Path>();
            n->loc = loc(ctx);
            n->segments = std::move(segments);
            return n;
        }
        if (ctx->LPAREN()) {
            if (auto* e = ctx->expression()) return buildExpr(e);
            auto n = std::make_shared<ast::UnitExpr>();
            n->loc = loc(ctx);
            return n;
        }
        if (auto* a = ctx->arrayExpression()) return buildArray(a);
        if (ctx->BREAK()) {
            auto n = std::make_shared<ast::Break>();
            n->loc = loc(ctx);
            if (auto* e = ctx->expression()) n->value = buildExpr(e);
            return n;
        }
        if (ctx->RETURN()) {
            auto n = std::make_shared<ast::Return>();
            n->loc = loc(ctx);
            if (auto* e = ctx->expression()) n->value = buildExpr(e);
            return n;
        }
        if (ctx->CONTINUE()) {
            auto n = std::make_shared<ast::Continue>();
            n->loc = loc(ctx);
            return n;
        }
        return nullptr;
    }

    ast::ExprPtr AstBuilder::buildConditionPrimary(RxParser::ConditionPrimaryWithoutBareBlockContext* ctx) {
        if (auto* lit = ctx->literalExpression()) return buildLiteral(lit);
        if (auto* p = ctx->pathInExpression()) {
            auto n = std::make_shared<ast::Path>();
            n->loc = loc(ctx);
            n->segments = buildPathInExpression(p);
            return n;
        }
        if (ctx->LPAREN()) {
            if (auto* e = ctx->expression()) return buildExpr(e);
            auto n = std::make_shared<ast::UnitExpr>();
            n->loc = loc(ctx);
            return n;
        }
        if (auto* a = ctx->arrayExpression()) return buildArray(a);
        if (auto* e = ctx->ifExpression()) return buildIf(e);
        if (ctx->LOOP()) {
            auto n = std::make_shared<ast::Loop>();
            n->loc = loc(ctx);
            n->body = buildBlock(ctx->blockExpression());
            return n;
        }
        if (ctx->WHILE()) {
            auto n = std::make_shared<ast::While>();
            n->loc = loc(ctx);
            n->condition = buildExpr(ctx->conditionExpression());
            n->body = buildBlock(ctx->blockExpression());
            return n;
        }
        if (ctx->BREAK()) {
            auto n = std::make_shared<ast::Break>();
            n->loc = loc(ctx);
            if (auto* e = ctx->conditionBreakExpression()) n->value = buildExpr(e);
            return n;
        }
        if (ctx->RETURN()) {
            auto n = std::make_shared<ast::Return>();
            n->loc = loc(ctx);
            if (auto* e = ctx->conditionExpression()) n->value = buildExpr(e);
            return n;
        }
        if (ctx->CONTINUE()) {
            auto n = std::make_shared<ast::Continue>();
            n->loc = loc(ctx);
            return n;
        }
        return nullptr;
    }

    ast::ExprPtr AstBuilder::buildLiteral(RxParser::LiteralExpressionContext* ctx) {
        auto n = std::make_shared<ast::Literal>();
        n->loc = loc(ctx);
        if (ctx->INTEGER_LITERAL()) {
            n->kind = ast::LiteralKind::Integer;
            n->text = ctx->INTEGER_LITERAL()->getText();
        } else if (ctx->TRUE()) {
            n->kind = ast::LiteralKind::Bool;
            n->text = "true";
        } else {
            n->kind = ast::LiteralKind::Bool;
            n->text = "false";
        }
        return n;
    }

    ast::ExprPtr AstBuilder::buildArray(RxParser::ArrayExpressionContext* ctx) {
        if (ctx->SEMI()) {
            auto n = std::make_shared<ast::ArrayRepeat>();
            n->loc = loc(ctx);
            n->element = buildExpr(ctx->expression(0));
            n->count = buildConstValue(ctx->constValue());
            return n;
        }
        auto n = std::make_shared<ast::ArrayLit>();
        n->loc = loc(ctx);
        for (auto* e : ctx->expression()) n->elements.push_back(buildExpr(e));
        return n;
    }

    ast::ExprPtr AstBuilder::buildExprWithBlock(ParserRuleContext* ctx) {
        switch (ctx->getRuleIndex()) {
            case RxParser::RuleBlockExpression:
                return buildBlock(static_cast<RxParser::BlockExpressionContext*>(ctx));
            case RxParser::RuleIfExpression:
                return buildIf(static_cast<RxParser::IfExpressionContext*>(ctx));
            case RxParser::RuleExpressionWithBlock: {
                auto* c = static_cast<RxParser::ExpressionWithBlockContext*>(ctx);
                if (auto* b = c->blockExpression()) return buildBlock(b);
                if (auto* i = c->ifExpression()) return buildIf(i);
                if (c->LOOP()) {
                    auto n = std::make_shared<ast::Loop>();
                    n->loc = loc(c);
                    n->body = buildBlock(c->blockExpression());
                    return n;
                }
                if (c->WHILE()) {
                    auto n = std::make_shared<ast::While>();
                    n->loc = loc(c);
                    n->condition = buildExpr(c->conditionExpression());
                    n->body = buildBlock(c->blockExpression());
                    return n;
                }
                return nullptr;
            }
            default:
                return nullptr;
        }
    }

    ast::ExprPtr AstBuilder::buildIf(RxParser::IfExpressionContext* ctx) {
        auto n = std::make_shared<ast::If>();
        n->loc = loc(ctx);
        n->condition = buildExpr(ctx->conditionExpression());
        n->thenBlock = buildBlock(ctx->blockExpression(0));
        if (ctx->ELSE()) {
            if (auto* e = ctx->blockExpression(1)) n->elseBranch = buildBlock(e);
            else if (auto* e = ctx->ifExpression()) n->elseBranch = buildIf(e);
        }
        return n;
    }

} // namespace rx
