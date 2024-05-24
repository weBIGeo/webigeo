#pragma once

#include "PropertyGroup.h"
#include <memory>

namespace dps {

class PropertyManager {
public:
    PropertyManager();

    std::shared_ptr<Property> get_root() const;

private:
    std::shared_ptr<PropertyGroup> root;
};

} // namespace dps

