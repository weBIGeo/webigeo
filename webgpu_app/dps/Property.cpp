#include "Property.h"

namespace dps {

Property::Property(const std::string& propertyName)
    : name(propertyName)
{
}

std::string Property::get_name() const { return name; }

std::shared_ptr<Property> Property::get_parent() const { return parent.lock(); }

void Property::add_child(const std::shared_ptr<Property>& child)
{
    std::lock_guard<std::mutex> lock(mtx);
    children.push_back(child);
    child->parent = shared_from_this();
}

const std::vector<std::shared_ptr<Property>>& Property::get_children() const { return children; }

} // namespace dps
