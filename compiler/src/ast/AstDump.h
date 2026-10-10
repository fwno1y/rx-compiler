#pragma once

#include <iosfwd>

#include "ast/AST.h"

namespace rx::ast {

    void dumpCrate(std::ostream& os, const Crate& crate);

}
