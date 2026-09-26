#pragma once

// Everything: every value kind in one struct, for the backends' lowering
// snapshots and their hostile-input sweeps.

#include "codec/harness/fixtures/scalars.h"
#include "fixtures/containers.h"
#include "fixtures/enums.h"
#include "fixtures/recursive.h"
#include "fixtures/structs.h"

namespace kota::test {

struct Everything {
    Scalars scalars;
    Strings strings;
    Bytes bytes;
    Color color;
    Nullables present;
    Nullables absent;
    Sequences sequences;
    Sets sets;
    Maps maps;
    Tuples tuples;
    TreeNode tree;
    Empty empty;

    static Everything typical() {
        TreeNode leaf{.value = "leaf", .children = {}};
        TreeNode unnamed{.value = "", .children = {}};
        return {
            .scalars = Scalars::typical(),
            .strings = Strings::typical(),
            .bytes = Bytes::typical(),
            .color = Color::green,
            .present = Nullables::engaged(),
            .absent = {},
            .sequences = Sequences::typical(),
            .sets = Sets::typical(),
            .maps = Maps::typical(),
            .tuples = Tuples::typical(),
            .tree = {.value = "root", .children = {leaf, unnamed}},
            .empty = {},
        };
    }
};

}  // namespace kota::test
