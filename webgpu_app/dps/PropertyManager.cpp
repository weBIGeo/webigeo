#include "PropertyManager.h"

namespace dps {

PropertyManager::PropertyManager() {
    root = std::make_shared<PropertyGroup>("root");
}

std::shared_ptr<Property> PropertyManager::get_root() const {
    return root;
}

} // namespace dps
