#pragma once
#include <atomic>
#include <boost/intrusive_ptr.hpp>
#include <boost/utility.hpp>
#include <cstddef>
#include <cstring>
#include <functional>
#include "rtti.h"
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace mill {
    class VM;

    class Value {
    public:
        Value() : referenceCount(1) { }

        virtual ~Value() = 0;

    private:
        mutable std::atomic<long> referenceCount;

        friend void retain(Value const&);
        friend void release(Value const&);
    };

    class Unit : public Value {
    public:
        static Unit& instance() {
            static Unit unit;
            return unit;
        }

    private:
        Unit() = default;
    };

    template<typename T>
    class CXXValue : public Value {
    public:
        explicit CXXValue(T value) : value(std::move(value)) { }

        T value;
    };

    using Boolean = CXXValue<bool const>;
    using String = CXXValue<std::string const>;

    class Subroutine : public Value {
    public:
        template<typename F>
        explicit Subroutine(F slow)
            : slow(std::move(slow)), fast(), fastAvailable(false) { }

        Subroutine(Subroutine const&) = delete;
        Subroutine& operator=(Subroutine const&) = delete;

        boost::intrusive_ptr<Value> operator()(VM& vm, std::size_t argc, Value** argv) {
            if (fastAvailable) {
                return fast(vm, argc, argv);
            } else {
                return slow(vm, argc, argv);
            }
        }

    private:
        std::function<boost::intrusive_ptr<Value>(VM&, std::size_t, Value**)> slow;
        std::function<boost::intrusive_ptr<Value>(VM&, std::size_t, Value**)> fast;
        std::atomic<bool> fastAvailable;

        friend class VM;
    };

    class Type : public Value {
    public:
        virtual ~Type() = 0;
    };
    inline Type::~Type() = default;

    class StructureType : public Type {
    public:
        StructureType(std::string name, std::vector<Type*> fields)
            : name(std::move(name))
            , fields(std::move(fields))
            , typeInfo(this->name.c_str(), static_cast<abi::__class_type_info const*>(&typeid(Value))) {
            vtable.offsetToTop = 0;
            vtable.typeInfo = &typeInfo;
            vtable.dtor1 = dtor;
            vtable.dtor2 = dtor;
        }

        boost::intrusive_ptr<Value> make() const {
            // First, initialize the value with the constructor of Value.
            // Value is abstract so we need to make a concrete subclass.
            class ConcreteValue : public Value { };
            auto memory = operator new(size());
            new (memory) ConcreteValue();

            // Next, patch the vptr of the object. This is a massive hack which
            // saves us a lot of work because we can nicely integrate with the
            // C++ type system at runtime.
            auto vptr = &vtable.dtor1;
            std::memcpy(memory, &vptr, sizeof(void*));

            return static_cast<Value*>(memory);
        }

        std::size_t size() const {
            auto padding = alignof(Value*) % sizeof(Value);
            return sizeof(Value) + padding + fields.size() * sizeof(Value*);
        }

    private:
        static void dtor(void* vself) {
            auto self = static_cast<Value*>(vself);
            // TODO: destroy all fields.
            self->Value::~Value();
        }

        std::string name;
        std::vector<Type*> fields;
        abi::__si_class_type_info typeInfo;
        struct {
            std::ptrdiff_t offsetToTop;
            std::type_info const* typeInfo;
            void (* dtor1)(void*);
            void (* dtor2)(void*);
        } vtable;
    };

    void retain(Value const&);
    void release(Value const&);

    void intrusive_ptr_add_ref(Value const*);
    void intrusive_ptr_release(Value const*);

    template<typename T, typename... Args>
    boost::intrusive_ptr<T> make(Args&&... args) {
        return new T(std::forward<Args>(args)...);
    }
}
