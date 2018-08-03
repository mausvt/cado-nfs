#ifndef LAS_DUPLICATE_HPP_
#define LAS_DUPLICATE_HPP_

#include "las-types.hpp"
#include "relation.hpp"
#include <memory>

int
relation_is_duplicate(relation const& rel,
        las_todo_entry const & doing,
        las_info const& las,
        facul_strategies_t const * old_strategies);

#endif
