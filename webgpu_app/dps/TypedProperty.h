#pragma once

#include "Property.h"
#include <glm/glm.hpp>
#include <sstream>
#include <type_traits>

namespace dps {

// Alias for types
using u32 = uint32_t;
using f32vec4 = glm::vec4;

template <typename T> class TypedProperty : public Property {
public:
    explicit TypedProperty(const std::string& propertyName, const T& initialValue)
        : Property(propertyName)
        , value(initialValue)
    {
    }

    std::string to_string() const override
    {
        std::lock_guard<std::mutex> lock(mtx);
        std::ostringstream oss;
        if constexpr (std::is_same_v<T, u32>) {
            oss << value;
        } else if constexpr (std::is_same_v<T, f32vec4>) {
            oss << value.x << "," << value.y << "," << value.z << "," << value.w;
        } else {
            oss << "Unsupported type";
        }
        return oss.str();
    }

    PropertyType type() const override
    {
        if constexpr (std::is_same_v<T, u32>) {
            return PropertyType::UINT32;
        } else if constexpr (std::is_same_v<T, f32vec4>) {
            return PropertyType::F32VEC4;
        } else {
            static_assert("Unsupported type");
        }
    }

    void set_value(const T& newValue)
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            value = newValue;
        }
        emit valueChanged();
    }

    T get_value() const
    {
        std::lock_guard<std::mutex> lock(mtx);
        return value;
    }

private:
    T value;
};

} // namespace dps
