#pragma once

#include <memory>
#include "data_structures/concurrent/concurrent_map.hpp"
#include "storage/model/properties/flags.hpp"
#include "utils/option.hpp"
#include "utils/total_ordering.hpp"
#include "utils/underlying_cast.hpp"

typedef Flags Type;

// Family of properties with the same name but different types.
// Ordered on name.
class PropertyFamily : public TotalOrdering<PropertyFamily>
{
    friend class PropertyType;
    friend class PropertyFamilyKey;
    friend class PropertyTypeKey;

public:
    // Type of property defined with his family and his type.
    // Ordered on PropertyFamily and Type.
    class PropertyType : public TotalOrdering<PropertyType>
    {
        friend class PropertyFamilyKey;
        friend class PropertyTypeKey;
        friend class PropertyFamily;

    public:
        // Ordered on POINTERS to PropertyFamily
        class PropertyFamilyKey : public TotalOrdering<PropertyFamilyKey>
        {
            friend class PropertyType;
            friend class PropertyTypeKey;

            PropertyFamilyKey(const PropertyType &type) : type(&type) {}

        public:
            friend bool operator==(const PropertyFamilyKey &lhs,
                                   const PropertyFamilyKey &rhs)
            {
                return &(lhs.type->family) == &(rhs.type->family);
            }

            friend bool operator<(const PropertyFamilyKey &lhs,
                                  const PropertyFamilyKey &rhs)
            {
                return &(lhs.type->family) < &(rhs.type->family);
            }

            Type prop_type() const { return type->type; }

            std::string const &family_name() const
            {
                return type->family.name();
            }

        private:
            const PropertyType *type;
        };

        // Ordered on POINTERS to PropertyType.
        // When compared with PropertyFamilyKey behaves as PropertyFamilyKey.
        template <class T>
        class PropertyTypeKey
            : public TotalOrdering<PropertyTypeKey<T>>,
              public TotalOrdering<PropertyFamilyKey, PropertyTypeKey<T>>,
              public TotalOrdering<PropertyTypeKey<T>, PropertyFamilyKey>
        {
            friend class PropertyType;

            PropertyTypeKey(const PropertyType &type) : type(type) {}
        public:
            PropertyFamilyKey family_key() { return PropertyFamilyKey(type); }

            Type prop_type() const { return type.type; }

            friend bool operator==(const PropertyTypeKey &lhs,
                                   const PropertyTypeKey &rhs)
            {
                return &(lhs.type) == &(rhs.type);
            }

            friend bool operator<(const PropertyTypeKey &lhs,
                                  const PropertyTypeKey &rhs)
            {
                return &(lhs.type) < &(rhs.type);
            }

            friend bool operator==(const PropertyFamilyKey &lhs,
                                   const PropertyTypeKey &rhs)
            {
                return &(lhs.type->family) == &(rhs.type.family);
            }

            friend bool operator<(const PropertyFamilyKey &lhs,
                                  const PropertyTypeKey &rhs)
            {
                return &(lhs.type->family) < &(rhs.type.family);
            }

            friend bool operator==(const PropertyTypeKey &lhs,
                                   const PropertyFamilyKey &rhs)
            {
                return &(lhs.type.family) == &(rhs.type->family);
            }

            friend bool operator<(const PropertyTypeKey &lhs,
                                  const PropertyFamilyKey &rhs)
            {
                return &(lhs.type.family) < &(rhs.type->family);
            }

        private:
            const PropertyType &type;
        };

    private:
        PropertyType(PropertyFamily &family, Type type);
        PropertyType(PropertyFamily &other) = delete;
        PropertyType(PropertyFamily &&other) = delete;

    public:
        template <class T>
        bool is() const
        {
            return underlying_cast(type) & underlying_cast(T::type);
        }

        bool is(Type &t) const;

        // Returns key ordered on POINTERS to PropertyType.
        // When compared with PropertyFamilyKey behaves as PropertyFamilyKey.
        template <class T>
        PropertyTypeKey<T> type_key()
        {
            assert(this->is<T>());
            return PropertyTypeKey<T>(*this);
        }

        // Returns key ordered on POINTERS to PropertyFamily
        PropertyFamilyKey family_key();

        friend bool operator<(const PropertyType &lhs, const PropertyType &rhs)
        {
            return lhs.family < rhs.family ||
                   (lhs.family == rhs.family && lhs.type < rhs.type);
        }

        friend bool operator==(const PropertyType &lhs, const PropertyType &rhs)
        {
            return lhs.family == rhs.family && lhs.type == rhs.type;
        }

    private:
        const PropertyFamily &family;
        const Type type;
    };

    PropertyFamily(std::string const &name_v);
    PropertyFamily(std::string &&name_v);
    PropertyFamily(PropertyFamily &other) = delete;
    PropertyFamily(PropertyFamily &&other) = delete;

    std::string const &name() const;

    // Returns type if it exists otherwise creates it.
    PropertyType &get(Type type);

    friend bool operator<(const PropertyFamily &lhs, const PropertyFamily &rhs);

    friend bool operator==(const PropertyFamily &lhs,
                           const PropertyFamily &rhs);

private:
    const std::string name_v;

    // TODO: Because types wont be removed this could be done with more efficent
    // data structure.
    ConcurrentMap<Type, std::unique_ptr<PropertyType>> types;
};
