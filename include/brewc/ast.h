#ifndef BREWC_AST_H
#define BREWC_AST_H

namespace brewc {

// forward declare the visitor so Expr can mention it before it's defined.
// the actual visit_* methods get added to it as we introduce each node type
// in the next few steps.
class Visitor;

// base class for every expression node. concrete nodes derive from this and
// implement accept(), which just turns around and calls the matching visit_*
// on whatever visitor is passed in (double dispatch).
class Expr {
public:
    virtual ~Expr() = default;
    virtual void accept(Visitor& visitor) = 0;
};

// anything that wants to walk the tree (the printer, the interpreter, ...)
// inherits from this. for now it's empty apart from the destructor since we
// don't have any concrete nodes yet; each node adds its own visit_* here.
class Visitor {
public:
    virtual ~Visitor() = default;
};

} // namespace brewc

#endif
