#pragma once

#include <cassert>
#include "database/db_accessor.hpp"
#include "storage/model/properties/property_family.hpp"
#include "storage/vertex_accessor.hpp"

// Holder for element data which he can then insert as a vertex or edge into the
// database depending on the available data.
class ElementSkeleton
{

    class Prop
    {

    public:
        Prop(VertexPropertyFamily::PropertyType::PropertyFamilyKey key,
             Option<std::shared_ptr<Property>> &&prop)
            : key_v(key), prop(std::move(prop))
        {
        }

        Prop(EdgePropertyFamily::PropertyType::PropertyFamilyKey key,
             Option<std::shared_ptr<Property>> &&prop)
            : key_e(key), prop(std::move(prop))
        {
        }

        union
        {
            VertexPropertyFamily::PropertyType::PropertyFamilyKey key_v;
            EdgePropertyFamily::PropertyType::PropertyFamilyKey key_e;
        };
        Option<std::shared_ptr<Property>> prop;
    };

public:
    ElementSkeleton(DbAccessor &db) : db(db){};

    void add_property(VertexPropertyFamily::PropertyType::PropertyFamilyKey key,
                      std::shared_ptr<Property> &&prop)
    {
        properties.push_back(Prop(key, make_option(std::move(prop))));
    }

    void add_property(EdgePropertyFamily::PropertyType::PropertyFamilyKey key,
                      std::shared_ptr<Property> &&prop)
    {
        properties.push_back(Prop(key, make_option(std::move(prop))));
    }

    void set_element_id(size_t id)
    {
        el_id = make_option<size_t>(std::move(id));
    }

    void add_label(Label const &label) { labels.push_back(&label); }

    void set_type(EdgeType const &type) { this->type = make_option(&type); }

    void set_from(VertexAccessor &&va)
    {
        from_va = make_option<VertexAccessor>(std::move(va));
    }

    void set_to(VertexAccessor &&va)
    {
        to_va = make_option<VertexAccessor>(std::move(va));
    }

    VertexAccessor add_vertex()
    {
        auto va = db.vertex_insert();

        for (auto l : labels) {
            // std::cout << *l << std::endl;
            va.add_label(*l);
        }

        for (auto prop : properties) {
            assert(prop.prop.is_present());
            va.set(prop.key_v, prop.prop.take());
        }

        return va;
    }

    // Return error msg if unsuccessful
    Option<std::string> add_edge()
    {
        if (!from_va.is_present()) {
            return make_option(std::string("From field must be seted"));
        }
        if (!to_va.is_present()) {
            return make_option(std::string("To field must be seted"));
        }

        auto ve = db.edge_insert(from_va.get(), to_va.get());
        if (type.is_present()) {
            ve.edge_type(*type.get());
        }

        for (auto prop : properties) {
            assert(prop.prop.is_present());
            ve.set(prop.key_e, prop.prop.take());
        }

        return make_option<std::string>();
    }

    void clear()
    {
        el_id = make_option<size_t>();
        to_va = make_option<VertexAccessor>();
        from_va = make_option<VertexAccessor>();
        type = make_option<EdgeType const *>();
        labels.clear();
        properties.clear();
    }

    // Returns import local id.
    Option<size_t> element_id() { return el_id; }

private:
    // template <class A>
    // void add_propreties(A &ra)
    // {
    //     for (auto prop : properties) {
    //         assert(prop.prop.is_present());
    //         ra.set(prop.key, prop.prop.take());
    //     }
    // }

    DbAccessor &db;

    Option<size_t> el_id;
    Option<VertexAccessor> to_va;
    Option<VertexAccessor> from_va;
    Option<EdgeType const *> type;
    std::vector<Label const *> labels;
    std::vector<Prop> properties;
};
