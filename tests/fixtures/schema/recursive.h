#pragma once

// Recursive-type fixtures — self-referential shapes via vector / ptr /
// optional / map / variant.

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kota::meta::fixtures {

struct TreeNode {
    std::string value;
    std::vector<TreeNode> children;
};

struct LinkedNode {
    int data;
    std::unique_ptr<LinkedNode> next;
};

struct SharedNode {
    std::string label;
    std::shared_ptr<SharedNode> parent;
    std::vector<std::shared_ptr<SharedNode>> children;
};

struct OptionalRecursive {
    int id;
    std::optional<std::vector<OptionalRecursive>> sub_items;
};

struct MapRecursive {
    std::string name;
    std::map<std::string, MapRecursive> nested;
};

struct VariantLeaf {
    int val;
};

struct VariantBranch {
    std::vector<std::variant<VariantLeaf, VariantBranch>> nodes;
};

struct MixedRecursive {
    std::string tag;
    std::optional<std::unique_ptr<MixedRecursive>> deep;
    std::map<std::string, std::vector<MixedRecursive>> grouped;
};

}  // namespace kota::meta::fixtures
