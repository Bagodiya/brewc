#ifndef BREWC_AST_PRINTER_H
#define BREWC_AST_PRINTER_H

#include <string>

#include "brewc/ast.h"

namespace brewc {

// walks the AST and turns it into an S-expression string so we can eyeball the
// tree while debugging the parser. e.g. `let x = 1 + 2` comes out as
// `(let x (+ 1 2))`. the format lines up with what the snapshot tests already
// check for, so the two don't drift apart.
//
// it implements both visitor interfaces because the tree mixes expressions and
// statements. each visit_* writes into buf_ and the recursive helpers reset it
// per node, so we don't have to thread a stringstream through every call.
class AstPrinter : public Visitor, public StmtVisitor {
public:
    std::string print(Expr& expr);
    std::string print(Stmt& stmt);

    void visit_literal(LiteralExpr& expr) override;
    void visit_identifier(IdentifierExpr& expr) override;
    void visit_binary(BinaryExpr& expr) override;
    void visit_unary(UnaryExpr& expr) override;
    void visit_call(CallExpr& expr) override;
    void visit_assign(AssignExpr& expr) override;

    void visit_let(LetStmt& stmt) override;
    void visit_if(IfStmt& stmt) override;
    void visit_while(WhileStmt& stmt) override;
    void visit_block(BlockStmt& stmt) override;
    void visit_fn(FnDecl& stmt) override;
    void visit_expr_stmt(ExprStmt& stmt) override;
    void visit_return(ReturnStmt& stmt) override;

private:
    std::string buf_;
};

} // namespace brewc

#endif
