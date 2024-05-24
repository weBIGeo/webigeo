#pragma once

#include "Property.h"
#include <functional>
#include <sstream>
#include <glm/glm.hpp> // Assuming glm is included and linked correctly

namespace dps {

// Alias for types
using u32 = uint32_t;
using f32vec4 = glm::vec4;

template <typename T>
class TypedProperty : public Property {
public:
    using Observer = std::function<void(const T&)>;

    explicit TypedProperty(const std::string& propertyName, const T& initialValue)
        : Property(propertyName), value(initialValue) {}

    std::string to_string() const override {
        std::lock_guard<std::mutex> lock(mtx);
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }

    PropertyType type() const override {
        if constexpr (std::is_same_v<T, u32>) {
            return PropertyType::UINT32;
        } else if constexpr (std::is_same_v<T, f32vec4>) {
            return PropertyType::F32VEC4;
        } else {
            static_assert("Unsupported type");
        }
    }

    void set_value(const T& newValue) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            value = newValue;
        }
        notify_observers(newValue);
    }

    T get_value() const {
        std::lock_guard<std::mutex> lock(mtx);
        return value;
    }

    void add_observer(Observer observer) {
        std::lock_guard<std::mutex> lock(mtx);
        observers.push_back(observer);
    }

private:
    void notify_observers(const T& newValue) {
        std::lock_guard<std::mutex> lock(mtx);
        for (const auto& observer : observers) {
            observer(newValue);
        }
    }

    T value;
    std::vector<Observer> observers;
    mutable std::mutex mtx;
};

} // namespace dps

