#pragma once

#include "PropertyGroup.h"
#include <memory>
#include <sstream>
#include <stack>

namespace dps {

class PropertyManager {
public:
    PropertyManager();

    std::shared_ptr<Property> get_root() const;

    std::string to_string() const;

private:
    std::shared_ptr<PropertyGroup> root;
};

} // namespace dps
