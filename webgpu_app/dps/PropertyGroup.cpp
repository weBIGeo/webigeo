#include "PropertyGroup.h"

namespace dps {

PropertyGroup::PropertyGroup(const std::string& propertyName)
    : Property(propertyName)
{
}

std::string PropertyGroup::to_string() const { return ""; }

PropertyType PropertyGroup::type() const { return PropertyType::GROUP; }

} // namespace dps
