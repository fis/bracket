/** \file
 * Deregister-on-destroy callbacks.
 */

#ifndef BASE_CALLBACK_H_
#define BASE_CALLBACK_H_

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>

#include "base/common.h"
#include "base/log.h"
#include "base/owner_set.h"

namespace base {

class Callback;

namespace internal {
struct CallbackContainer {
  virtual void Unregister(Callback* dying_callback, void* data) = 0;
};
} // namespace internal

/**
 * Base class for callback interfaces.
 *
 * Any callback interfaces registered to the callback containers (CallbackPtr, CallbackSet) must
 * have this class as a base class. They should probably use virtual inheritance, in case the client
 * wants to use a single class to implement more than one callback interface.
 *
 * This class contains the support code for the callback to unregister itself from any callback
 * container it's been added to when it gets destroyed. Conversely, callback containers know how to
 * undo the linkage from their callbacks when they themselves get destroyed.
 */
class Callback {
 public:
  virtual ~Callback() {
    DetachCallback();
  }

  /** Immediately detaches the callback from any containers it has been added to. */
  void DetachCallback() {
    for (auto&& item : containers_)
      item.first->Unregister(this, item.second);
    containers_.clear();
  }

 private:
  std::map<internal::CallbackContainer*, void*> containers_;

  void RegisterContainer(internal::CallbackContainer* container, void* data) {
    auto [iter, inserted] = containers_.try_emplace(container, data);
    (void) iter;
    CHECK(inserted);
  }

  void UnregisterContainer(internal::CallbackContainer* container) {
    bool erased = containers_.erase(container);
    CHECK(erased);
  }

  template<typename Iface>
  friend class CallbackPtr;
  template<typename Iface, typename... Datas>
  friend class CallbackSet;
  template<typename Key, typename Iface>
  friend class CallbackMap;
};

/**
 * Callback container holding (and optionally owning) a single callback.
 *
 * \tparam Iface callback interface, deriving from Callback.
 */
template<typename Iface>
class CallbackPtr : public internal::CallbackContainer {
 public:
  explicit CallbackPtr(base::optional_ptr<Iface> callback = nullptr) {
    if (callback)
      Set(std::move(callback));
  }
  DISALLOW_COPY(CallbackPtr);

  ~CallbackPtr() {
    Clear();
  }

  /**
   * Sets the contained callback.
   *
   * If the container already owned a callback, it's destroyed. If it held but did not own a
   * callback, that callback will stop being linked to this container, but will not be destroyed.
   */
  void Set(base::optional_ptr<Iface> callback) {
    if (callback_)
      Clear();
    callback_ = std::move(callback);
    callback_->RegisterContainer(this, nullptr);
  }

  /**
   * Removes the callback from this container.
   *
   * If the callback was owned, it's destroyed. Otherwise it's simply unlinked from this
   * container. If the container was empty, nothing happens.
   */
  void Clear() {
    if (callback_) {
      callback_->UnregisterContainer(this);
      callback_ = nullptr;
    }
  }

  /**
   * Calls \p f with the contained callback.
   *
   * If the container is empty, nothing happens.
   *
   * \tparam F callable type suitable to be called as `f(Iface*)`
   * \param f callback object
   */
  template<typename F>
  void With(F f) {
    if (callback_)
      f(callback_.get());
  }

  /**
   * Calls one of the callback interface methods on the contained callback.
   *
   * If the container is empty, nothing happens.
   *
   * Given `struct Iface : public virtual Callback { virtual void f(int x) = 0; }`, you can call the
   * method with `ptr.Call(&Iface::f, 123)`.
   *
   * \tparam M pointer to a member function type of \p Iface
   * \tparam Args argument types of the callback method
   * \param m pointer to a member
   * \param args arguments forwarded to the called method
   */
  template<typename M, typename... Args>
  void Call(M m, Args&&... args) {
    if (callback_)
      ((*callback_).*m)(std::forward<Args>(args)...);
  }

  /**
   * Removes a callback from this container.
   *
   * Only intended to be called by code in the Callback base class.
   */
  void Unregister(Callback* dying_callback, void* data) override {
    if (callback_.get() == dying_callback)
      callback_ = nullptr;
  }

  /** Returns `true` if no callback has been set. */
  bool empty() const noexcept { return !callback_; }

 private:
  base::optional_ptr<Iface> callback_ = nullptr;
};

/**
 * Callback container holding (and optionally owning) a set of callbacks, with associated data.
 *
 * Ownership can be set on each callback separately.
 *
 * \tparam Iface callback interface, deriving from Callback
 * \tparam Datas extra data types to include with each callback
 */
template<typename Iface, typename... Datas>
class CallbackSet : public internal::CallbackContainer {
 public:
  CallbackSet() {}
  DISALLOW_COPY(CallbackSet);

  ~CallbackSet() {
    for (auto&& holder : callbacks_)
      holder.second.callback->UnregisterContainer(this);
  }

  /**
   * Adds a callback to the set.
   *
   * \tparam CDatas extra data types, must be convertible to \p Datas
   * \param callback callback to add to the set
   * \param datas initializers for any extra data
   */
  template<typename... CDatas>
  void Add(base::optional_ptr<Iface> callback, CDatas&&... datas) {
    auto [holder, inserted] = callbacks_.try_emplace(callback.get(), std::move(callback), std::forward<CDatas>(datas)...);
    CHECK(inserted);
    holder->second.callback->RegisterContainer(this, holder->second.callback.get());
  }

  /** Removes a callback from the set. */
  bool Remove(Iface* callback) {
    auto holder = callbacks_.extract(callback);
    if (!holder)
      return false;
    // TODO: maybe move this logic to CallbackHolder as well
    holder.mapped().callback->UnregisterContainer(this);
    return true;
  }

  /**
   * Calls \p f with each callback in the set.
   *
   * \tparam F callable type suitable to be called as `f(Iface*)`
   * \param f callback object
   */
  template<typename F>
  void For(F f) {
    for (auto&& holder : callbacks_)
      f(holder.second.callback.get());
  }

  /**
   * Calls one of the callback interface methods on every contained callback.
   *
   * Given `struct Iface : public virtual Callback { virtual void f(int x) = 0; }`, you can call the
   * method with `set.Call(&Iface::f, 123)`.
   *
   * \tparam M pointer to a member function type of \p Iface
   * \tparam Args argument types of the callback method
   * \param m pointer to a member
   * \param args arguments forwarded to the called method
   */
  template<typename M, typename... Args>
  unsigned Call(M m, Args&&... args) {
    unsigned count = 0;
    for (auto&& holder : callbacks_) {
      ((*holder.second.callback).*m)(std::forward<Args>(args)...);
      ++count;
    }
    return count;
  }

  /**
   * Calls a method on those callbacks where a predicate returns `true` on the attached extra data.
   *
   * \tparam Pred predicate callable type
   * \tparam M pointer to a member function type of \p Iface
   * \tparam Args argument types of the callback method
   * \param pred predicate callable, called with the associated \p Datas
   * \param m pointer to a member
   * \param args arguments forwarded to the called method
   */
  template<typename Pred, typename M, typename... Args>
  unsigned CallIf(Pred pred, M m, Args&&... args) {
    unsigned count = 0;
    for (auto&& holder : callbacks_) {
      if (std::apply(pred, holder.second.data)) {
        ((*holder.second.callback).*m)(std::forward<Args>(args)...);
        ++count;
      }
    }
    return count;
  }

  /**
   * Removes a callback from this container.
   *
   * Only intended to be called by code in the Callback base class.
   */
  void Unregister(Callback* dying_callback, void* data) override {
    callbacks_.erase(static_cast<Iface*>(data));
  }

  /** Returns `true` if the set contains no callbacks. */
  bool empty() const noexcept { return callbacks_.empty(); }

 private:
  struct CallbackHolder {
    base::optional_ptr<Iface> callback;
    std::tuple<Datas...> data;
    template<typename... CDatas>
    CallbackHolder(base::optional_ptr<Iface> callback, CDatas&&... args)
        : callback(std::move(callback)), data(std::forward<CDatas>(args)...)
    {}
    DISALLOW_COPY(CallbackHolder);
  };

  std::unordered_map<Iface*, CallbackHolder> callbacks_;
};

/**
 * Callback container holding (and optionally owning) a map from a key to a callback.
 *
 * Values of the key type are contained in the map. Ownership of the callback pointers can be set on
 * each callback separately.
 *
 * \tparam Key key type for the map
 * \tparam Iface callback interface, deriving from Callback
 */
template<typename Key, typename Iface>
class CallbackMap : public internal::CallbackContainer {
 public:
  CallbackMap() {}
  DISALLOW_COPY(CallbackMap);

  ~CallbackMap() {
    for (auto&& cb : callbacks_) {
      cb.second->UnregisterContainer(this);
    }
  }

  /** Adds a callback to the map. */
  void Add(const Key& key, base::optional_ptr<Iface> callback) {
    auto [iter, inserted] = callbacks_.try_emplace(key, std::move(callback));
    if (!inserted) {
      // TODO: modify existing entry
    }
    iter->second->RegisterContainer(this, (void*)&iter->first); // TODO figure out cast
  }
  // TODO Key&& overload

  bool Remove(const Key& key) {
    auto node = callbacks_.extract(key);
    if (!node)
      return false;
    node.mapped()->UnregisterContainer(this);
    return true;
  }

  /**
   * Calls one of the callback interface methods on the callback for \p key.
   *
   * Given `struct Iface : public virtual Callback { virtual void f(int x) = 0; }`, you can call the
   * method with `map.Call(key, &Iface::f, 123)`.
   *
   * \tparam M pointer to a member function type of \p Iface
   * \tparam Args argument types of the callback method
   * \param key key to look up
   * \param m pointer to a member
   * \param args arguments forwarded to the called method
   * \return `true` if there was a callback to call, `false` otherwise
   */
  template<typename M, typename... Args>
  bool Call(const Key& key, M m, Args&&... args) {
    auto iter = callbacks_.find(key);
    if (iter == callbacks_.end())
      return false;
    ((*iter->second).*m)(std::forward<Args>(args)...);
    return true;
  }

  /**
   * Removes a callback from this container.
   *
   * Only intended to be called by code in the Callback base class.
   */
  void Unregister(Callback* dying_callback, void* data) override {
    bool erased = callbacks_.erase(*static_cast<Key*>(data));
    CHECK(erased);
  }

  /** Returns `true` if the map contains no callbacks. */
  bool empty() const noexcept { return callbacks_.empty(); }

 private:
  std::unordered_map<Key, base::optional_ptr<Iface>> callbacks_;
};


/**
 * Helper template to create wrappers from `std::function` to callbacks.
 *
 * Typically used with the `CALLBACK_*` series of macros.
 *
 * Example for a single-method interface:
 *
 *     struct Foo {
 *       virtual void Bar(int) = 0;
 *     };
 *     CALLBACK_F1(FooF, Foo, Bar, int);
 *
 * This defines a `FooF` class, which derives from `Foo`. The class has a single-argument
 * constructor accepting a `const std::function<void(int)>&`, which it uses to initialize a
 * `std::function` object. When the `Bar` method is called, it is delegated to the stored function.
 *
 * Example for a two-method interface:
 *
 *     struct Foo {
 *       virtual void Bar(int, float) = 0;
 *       virtual void Baz(char) = 0;
 *     };
 *     CALLBACK_F(FooF, Foo) {
 *       CALLBACK_C(FooF);
 *       CALLBACK_M2(0, Bar, int, float);
 *       CALLBACK_M1(1, Baz, char);
 *     };
 *
 * This defines a `FooF` class, which derives from `Foo`. The class has a two-argument constructor,
 * accepting a `const std::function<void(int,float)>&` and a `const std::function<void(char)>&`,
 * which it uses to initialize two `std::function` objects. When the `Bar` method is called, it's
 * delegated to the first function. When the `Baz` method is called, it's delegated to the second
 * one.
 */
template<typename... Funcs>
class CallbackF {
 protected:
  std::tuple<std::function<Funcs>...> f_;
 public:
  template<typename... CFuncs>
  CallbackF(CFuncs&&... args) : f_(std::forward<CFuncs>(args)...) {}
};

/** Starts a callback wrapper class definition, see CallbackF. */
#define CALLBACK_F(Name, Iface, ...) struct Name : public Iface, public ::base::CallbackF<__VA_ARGS__>
/** Defines a callback wrapper constructor, see CallbackF. */
#define CALLBACK_C(Name) template<typename... CFuncs> explicit Name(CFuncs&&... args) : CallbackF(std::forward<CFuncs>(args)...) {}
/** Defines a single-argument method in a callback wrapper class, see CallbackF. */
#define CALLBACK_M1(n, Method, T) void Method(T a) override { std::get<n>(f_)(std::forward<T>(a)); }
/** Defines a single-argument method in a callback wrapper class, see CallbackF. */
#define CALLBACK_M2(n, Method, T1, T2) void Method(T1 a, T2 b) override { std::get<n>(f_)(std::forward<T1>(a), std::forward<T2>(b)); }

/** Fully defines a callback wrapper class for single method with single argument, see CallbackF. */
#define CALLBACK_F1(Name, Iface, Method, T) \
  CALLBACK_F(Name, Iface, void(T)) { CALLBACK_C(Name) CALLBACK_M1(0, Method, T) }

} // namespace base

#endif // BASE_CALLBACK_H_

// Local Variables:
// mode: c++
// End:
