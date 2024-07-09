#pragma once

#include "Property.h"

namespace dps {

class PropertyGroup : public Property {
public:
    explicit PropertyGroup(const std::string& propertyName);

    std::string to_string() const override;
    PropertyType type() const override;
};

} // namespace dps
