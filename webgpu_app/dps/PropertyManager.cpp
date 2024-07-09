#include "PropertyManager.h"
#include "PropertyGroup.h"
#include "TypedProperty.h"
#include <iostream>

namespace dps {

PropertyManager::PropertyManager()
    : root(std::make_shared<PropertyGroup>("Root"))
{
}

std::shared_ptr<Property> PropertyManager::get_root() const { return root; }

std::string PropertyManager::to_string() const
{
    std::ostringstream oss;
    std::stack<std::pair<std::shared_ptr<Property>, int>> stack;

    stack.push({ root, 0 });

    while (!stack.empty()) {
        auto [property, level] = stack.top();
        stack.pop();

        if (!property)
            continue;

        if (level == 0) {
            // No indentation for the root
            oss << property->get_name() << " (" << static_cast<int>(property->type()) << ")";
        } else {
            // Create the indentation string
            std::string indent;
            for (int i = 1; i < level; ++i) {
                indent += "|  ";
            }

            oss << indent << "|-- " << property->get_name() << " (" << static_cast<int>(property->type()) << ")";
        }
        auto value_str = property->to_string();
        if (!value_str.empty())
            oss << " = " << value_str;

        oss << "\n";

        const auto& children = property->get_children();
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.push({ *it, level + 1 });
        }
    }

    return oss.str();
}

} // namespace dps
