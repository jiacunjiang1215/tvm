/*!
 *  Copyright (c) 2017 by Contributors
 * \file packed_func.h
 * \brief Type-erased function used across TVM API.
 */
#ifndef TVM_RUNTIME_PACKED_FUNC_H_
#define TVM_RUNTIME_PACKED_FUNC_H_

#include <dmlc/logging.h>
#include <functional>
#include <tuple>
#include <vector>
#include <string>
#include <limits>
#include <memory>
#include <type_traits>
#include "./c_runtime_api.h"
#include "./module.h"

namespace Halide {
// Forward declare type for extensions
// The header works fine without depending on this.
struct Type;
struct Expr;
}

namespace tvm {
// Forward declare NodeRef and Node for extensions.
// This header works fine without depend on NodeRef
// as long as it is not used.
class Node;
class NodeRef;

namespace runtime {
// forward declarations
class TVMArgs;
class TVMArgValue;
class TVMRetValue;
class TVMArgsSetter;

/*!
 * \brief Packed function is a type-erased function.
 *  The arguments are passed by packed format.
 *
 *  This is an useful unified interface to call generated functions,
 *  It is the unified function function type of TVM.
 *  It corresponds to TVMFunctionHandle in C runtime API.
 */
class PackedFunc {
 public:
  /*!
   * \brief The internal std::function
   * \param args The arguments to the function.
   * \param rv The return value.
   *
   * \code
   *   // Example code on how to implemented FType
   *   void MyPackedFunc(TVMArgs args, TVMRetValue* rv) {
   *     // automatically convert arguments to desired type.
   *     int a0 = args[0];
   *     float a1 = args[1];
   *     ...
   *     // automatically assign values to rv
   *     std::string my_return_value = "x";
   *     *rv = my_return_value;
   *   }
   * \endcode
   */
  using FType = std::function<void (TVMArgs args, TVMRetValue* rv)>;
  /*! \brief default constructor */
  PackedFunc() {}
  /*!
   * \brief constructing a packed function from a std::function.
   * \param body the internal container of packed function.
   */
  explicit PackedFunc(FType body) : body_(body) {}
  /*!
   * \brief Call packed function by directly passing in unpacked format.
   * \param args Arguments to be passed.
   * \tparam Args arguments to be passed.
   *
   * \code
   *   // Example code on how to call packed function
   *   void CallPacked(PackedFunc f) {
   *     // call like normal functions by pass in arguments
   *     // return value is automatically converted back
   *     int rvalue = f(1, 2.0);
   *   }
   * \endcode
   */
  template<typename... Args>
  inline TVMRetValue operator()(Args&& ...args) const;
  /*!
   * \brief Call the function in packed format.
   * \param args The arguments
   * \param rv The return value.
   */
  inline void CallPacked(TVMArgs args, TVMRetValue* rv) const;
  /*! \return the internal body function */
  inline FType body() const;
  /*! \return Whether the packed function is nullptr */
  bool operator==(std::nullptr_t null) const {
    return body_ == nullptr;
  }
  /*! \return Whether the packed function is not nullptr */
  bool operator!=(std::nullptr_t null) const {
    return body_ != nullptr;
  }

 private:
  /*! \brief internal container of packed function */
  FType body_;
};

/*! \brief Arguments into TVM functions. */
class TVMArgs {
 public:
  const TVMValue* values;
  const int* type_codes;
  int num_args;
  /*!
   * \brief constructor
   * \param values The argument values
   * \param type_codes The argument type codes
   * \param num_args number of arguments.
   */
  TVMArgs(const TVMValue* values,
          const int* type_codes,
          int num_args)
      : values(values),
        type_codes(type_codes),
        num_args(num_args) { }
  /*! \return size of the arguments */
  inline int size() const;
  /*!
   * \brief Get i-th argument
   * \param i the index.
   * \return the ith argument.
   */
  inline TVMArgValue operator[](int i) const;
};

/*!
 * \brief Convert type code to its name
 * \param type_code The type code .
 * \return The name of type code.
 */
inline const char* TypeCode2Str(int type_code);

/*!
 * \brief convert a string to TVM type.
 * \param s The string to be converted.
 * \return The corresponding tvm type.
 */
inline TVMType String2TVMType(std::string s);

/*!
 * \brief convert a TVM type to string.
 * \param t The type to be converted.
 * \return The corresponding tvm type in string.
 */
inline std::string TVMType2String(TVMType t);

// macro to check type code.
#define TVM_CHECK_TYPE_CODE(CODE, T)                           \
  CHECK_EQ(CODE, T) << " expected "                            \
  << TypeCode2Str(T) << " but get " << TypeCode2Str(CODE)      \

/*!
 * \brief Internal base class to
 *  handle conversion to POD values.
 */
class TVMPODValue_ {
 public:
  operator double() const {
    TVM_CHECK_TYPE_CODE(type_code_, kFloat);
    return value_.v_float64;
  }
  operator int64_t() const {
    TVM_CHECK_TYPE_CODE(type_code_, kInt);
    return value_.v_int64;
  }
  operator uint64_t() const {
    TVM_CHECK_TYPE_CODE(type_code_, kInt);
    return value_.v_int64;
  }
  operator int() const {
    TVM_CHECK_TYPE_CODE(type_code_, kInt);
    CHECK_LE(value_.v_int64,
             std::numeric_limits<int>::max());
    return static_cast<int>(value_.v_int64);
  }
  operator bool() const {
    TVM_CHECK_TYPE_CODE(type_code_, kInt);
    return value_.v_int64 != 0;
  }
  operator void*() const {
    if (type_code_ == kNull) return nullptr;
    if (type_code_ == kArrayHandle) return value_.v_handle;
    TVM_CHECK_TYPE_CODE(type_code_, kHandle);
    return value_.v_handle;
  }
  operator TVMArray*() const {
    if (type_code_ == kNull) return nullptr;
    TVM_CHECK_TYPE_CODE(type_code_, kArrayHandle);
    return static_cast<TVMArray*>(value_.v_handle);
  }
  int type_code() const {
    return type_code_;
  }

 protected:
  friend class TVMArgsSetter;
  friend class TVMRetValue;
  TVMPODValue_() : type_code_(kNull) {}
  TVMPODValue_(TVMValue value, int type_code)
      : value_(value), type_code_(type_code) {}
  /*!
   * \brief return handle as specific pointer type.
   * \tparam T the data type.
   * \return The pointer type.
   */
  template<typename T>
  T* ptr() const {
    return static_cast<T*>(value_.v_handle);
  }
  /*! \brief The value */
  TVMValue value_;
  /*! \brief the type code */
  int type_code_;
};

/*!
 * \brief A single argument value to PackedFunc.
 *  Containing both type_code and TVMValue
 *
 *  Provides utilities to do type cast into other types.
 */
class TVMArgValue : public TVMPODValue_ {
 public:
  /*!
   * \brief constructor
   * \param value of the function
   * \param type_code The type code.
   */
  TVMArgValue(TVMValue value, int type_code)
      : TVMPODValue_(value, type_code) {
  }
  // reuse converter from parent
  using TVMPODValue_::operator double;
  using TVMPODValue_::operator int64_t;
  using TVMPODValue_::operator uint64_t;
  using TVMPODValue_::operator int;
  using TVMPODValue_::operator bool;
  using TVMPODValue_::operator void*;
  using TVMPODValue_::operator TVMArray*;
  // conversion operator.
  operator std::string() const {
    if (type_code_ == kTVMType) {
      return TVMType2String(operator TVMType());
    } else if (type_code_ == kBytes) {
      TVMByteArray* arr = static_cast<TVMByteArray*>(value_.v_handle);
      return std::string(arr->data, arr->size);
    } else {
      TVM_CHECK_TYPE_CODE(type_code_, kStr);
      return std::string(value_.v_str);
    }
  }
  operator TVMType() const {
    if (type_code_ == kStr) {
      return String2TVMType(operator std::string());
    }
    TVM_CHECK_TYPE_CODE(type_code_, kTVMType);
    return value_.v_type;
  }
  operator PackedFunc() const {
    TVM_CHECK_TYPE_CODE(type_code_, kFuncHandle);
    return *ptr<PackedFunc>();
  }
  operator Module() const {
    TVM_CHECK_TYPE_CODE(type_code_, kModuleHandle);
    return *ptr<Module>();
  }
  const TVMValue& value() const {
    return value_;
  }
  // NodeRef related extenstions: in tvm/packed_func_ext.h
  template<typename TNodeRef,
           typename = typename std::enable_if<
             std::is_class<TNodeRef>::value>::type>
  inline operator TNodeRef() const;
  template<typename TNodeRef,
           typename = typename std::enable_if<
             std::is_class<TNodeRef>::value>::type>
  inline bool IsNodeType() const;
  inline operator Halide::Type() const;
  inline operator Halide::Expr() const;
  // get internal node ptr, if it is node
  inline std::shared_ptr<Node>& node_sptr();
};

/*!
 * \brief Return Value container,
 *  Unlike TVMArgValue, which only holds reference and do not delete
 *  the underlying container during destruction.
 *
 *  TVMRetValue holds value and will manage the underlying containers
 *  when it stores a complicated data type.
 */
class TVMRetValue : public TVMPODValue_ {
 public:
  /*! \brief default constructor */
  TVMRetValue() {}
  /*!
   * \brief move constructor from anoter return value.
   * \param other The other return value.
   */
  TVMRetValue(TVMRetValue&& other)
      : TVMPODValue_(other.value_, other.type_code_) {
  }
  /*! \brief destructor */
  ~TVMRetValue() {
    this->Clear();
  }
  // reuse converter from parent
  using TVMPODValue_::operator double;
  using TVMPODValue_::operator int64_t;
  using TVMPODValue_::operator uint64_t;
  using TVMPODValue_::operator int;
  using TVMPODValue_::operator bool;
  using TVMPODValue_::operator void*;
  using TVMPODValue_::operator TVMArray*;
  // Disable copy and assign from another value, but allow move.
  TVMRetValue(const TVMRetValue& other) {
    this->Assign(other);
  }
  // conversion operators
  operator std::string() const {
    if (type_code_ == kTVMType) {
      return TVMType2String(operator TVMType());
    }
    TVM_CHECK_TYPE_CODE(type_code_, kStr);
    return *ptr<std::string>();
  }
  operator TVMType() const {
    if (type_code_ == kStr) {
      return String2TVMType(operator std::string());
    }
    TVM_CHECK_TYPE_CODE(type_code_, kTVMType);
    return value_.v_type;
  }
  operator PackedFunc() const {
    TVM_CHECK_TYPE_CODE(type_code_, kFuncHandle);
    return *ptr<PackedFunc>();
  }
  operator Module() const {
    TVM_CHECK_TYPE_CODE(type_code_, kModuleHandle);
    return *ptr<Module>();
  }
  // Assign operators
  TVMRetValue& operator=(TVMRetValue&& other) {
    this->Clear();
    value_ = other.value_;
    type_code_ = other.type_code_;
    other.type_code_ = kNull;
    return *this;
  }
  TVMRetValue& operator=(double value) {
    this->SwitchToPOD(kFloat);
    value_.v_float64 = value;
    return *this;
  }
  TVMRetValue& operator=(std::nullptr_t value) {
    this->SwitchToPOD(kNull);
    value_.v_handle = value;
    return *this;
  }
  TVMRetValue& operator=(void* value) {
    this->SwitchToPOD(kHandle);
    value_.v_handle = value;
    return *this;
  }
  TVMRetValue& operator=(int64_t value) {
    this->SwitchToPOD(kInt);
    value_.v_int64 = value;
    return *this;
  }
  TVMRetValue& operator=(int value) {
    this->SwitchToPOD(kInt);
    value_.v_int64 = value;
    return *this;
  }
  TVMRetValue& operator=(TVMType t) {
    this->SwitchToPOD(kTVMType);
    value_.v_type = t;
    return *this;
  }
  TVMRetValue& operator=(bool value) {
    this->SwitchToPOD(kInt);
    value_.v_int64 = value;
    return *this;
  }
  TVMRetValue& operator=(std::string value) {
    this->SwitchToClass(kStr, value);
    return *this;
  }
  TVMRetValue& operator=(PackedFunc f) {
    this->SwitchToClass(kFuncHandle, f);
    return *this;
  }
  TVMRetValue& operator=(Module m) {
    this->SwitchToClass(kModuleHandle, m);
    return *this;
  }
  TVMRetValue& operator=(const TVMRetValue& other) {  // NOLINT(*0
    this->Assign(other);
    return *this;
  }
  TVMRetValue& operator=(TVMArgValue other) {
    this->Assign(other);
    return *this;
  }
  /*!
   * \brief Move the value back to front-end via C API.
   *  This marks the current container as null.
   *  The managed resources is moved to front-end and
   *  the front end should take charge in managing them.
   *
   * \param ret_value The return value.
   * \param ret_type_code The return type code.
   */
  void MoveToCHost(TVMValue* ret_value,
                   int* ret_type_code) {
    // cannot move str; need specially handle.
    CHECK(type_code_ != kStr);
    *ret_value = value_;
    *ret_type_code = type_code_;
    type_code_ = kNull;
  }
  /*! \return The value field, if the data is POD */
  const TVMValue& value() const {
    CHECK(type_code_ != kNodeHandle &&
          type_code_ != kFuncHandle &&
          type_code_ != kModuleHandle &&
          type_code_ != kStr) << "TVMRetValue.value can only be used for POD data";
    return value_;
  }
  // NodeRef related extenstions: in tvm/packed_func_ext.h
  inline TVMRetValue& operator=(const NodeRef& other);
  inline TVMRetValue& operator=(const std::shared_ptr<Node>& other);
  template<typename TNodeRef,
           typename = typename std::enable_if<
             std::is_class<TNodeRef>::value>::type>
  inline operator TNodeRef() const;
  // type related
  inline operator Halide::Type() const;
  inline TVMRetValue& operator=(const Halide::Type& other);

 private:
  template<typename T>
  void Assign(const T& other) {
    switch (other.type_code()) {
      case kStr:
      case kBytes: {
        SwitchToClass<std::string>(kStr, other);
        break;
      }
      case kFuncHandle: {
        SwitchToClass<PackedFunc>(kFuncHandle, other);
        break;
      }
      case kModuleHandle: {
        SwitchToClass<PackedFunc>(kModuleHandle, other);
        break;
      }
      case kNodeHandle: {
        SwitchToClass<std::shared_ptr<Node> >(
            kNodeHandle, *other.template ptr<std::shared_ptr<Node> >());
        break;
      }
      default: {
        SwitchToPOD(other.type_code());
        value_ = other.value_;
        break;
      }
    }
  }

  // get the internal container.
  void SwitchToPOD(int type_code) {
    if (type_code_ != type_code) {
      this->Clear();
      type_code_ = type_code;
    }
  }
  template<typename T>
  void SwitchToClass(int type_code, T v) {
    if (type_code_ != type_code) {
      this->Clear();
      type_code_ = type_code;
      value_.v_handle = new T(v);
    } else {
      *static_cast<T*>(value_.v_handle) = v;
    }
  }
  void Clear() {
    if (type_code_ == kNull) return;
    switch (type_code_) {
      case kStr: delete ptr<std::string>(); break;
      case kFuncHandle: delete ptr<PackedFunc>(); break;
      case kModuleHandle: delete ptr<Module>(); break;
      case kNodeHandle: delete ptr<std::shared_ptr<Node> >(); break;
    }
    type_code_ = kNull;
  }
};

// implementation details
inline const char* TypeCode2Str(int type_code) {
  switch (type_code) {
    case kInt: return "int";
    case kFloat: return "float";
    case kStr: return "str";
    case kBytes: return "bytes";
    case kHandle: return "handle";
    case kNull: return "NULL";
    case kNodeHandle: return "NodeHandle";
    case kArrayHandle: return "ArrayHandle";
    case kTVMType: return "TVMType";
    case kFuncHandle: return "FunctionHandle";
    case kModuleHandle: return "ModuleHandle";
    default: LOG(FATAL) << "unknown type_code="
                        << static_cast<int>(type_code); return "";
  }
}

inline std::ostream& operator<<(std::ostream& os, TVMType t) {  // NOLINT(*)
  os << TypeCode2Str(t.code);
  if (t.code == kHandle) return os;
  os << static_cast<int>(t.bits);
  if (t.lanes != 1) {
    os << 'x' << static_cast<int>(t.lanes);
  }
  return os;
}

inline std::string TVMType2String(TVMType t) {
  std::ostringstream os;
  os << t;
  return os.str();
}

inline TVMType String2TVMType(std::string s) {
  TVMType t;
  t.bits = 32; t.lanes = 1;
  const char* scan;
  if (s.substr(0, 3) == "int") {
    t.code = kInt;  scan = s.c_str() + 3;
  } else if (s.substr(0, 4) == "uint") {
    t.code = kUInt; scan = s.c_str() + 4;
  } else if (s.substr(0, 5) == "float") {
    t.code = kFloat; scan = s.c_str() + 5;
  } else if (s.substr(0, 6) == "handle") {
    t.code = kHandle;
    t.bits = 64;  // handle uses 64 bit by default.
    scan = s.c_str() + 6;
  } else {
    scan = s.c_str();
    LOG(FATAL) << "unknown type " << s;
  }
  unsigned bits = t.bits, lanes = t.lanes;
  sscanf(scan, "%ux%u", &bits, &lanes);
  t.bits = static_cast<uint8_t>(bits);
  t.lanes = static_cast<uint16_t>(lanes);
  return t;
}

inline TVMArgValue TVMArgs::operator[](int i) const {
  CHECK_LT(i, num_args)
      << "not enough argument passed, "
      << num_args << " passed"
      << " but request arg[" << i << "].";
  return TVMArgValue(values[i], type_codes[i]);
}

inline int TVMArgs::size() const {
  return num_args;
}

inline void PackedFunc::CallPacked(TVMArgs args, TVMRetValue* rv) const {
  body_(args, rv);
}

inline PackedFunc::FType PackedFunc::body() const {
  return body_;
}

// internal namespace
namespace detail {
template<bool stop, std::size_t I, typename F, typename ...Args>
struct for_each_dispatcher {
  static void run(std::tuple<Args...>& args, const F& f) {  // NOLINT(*)
    f(I, std::get<I>(args));
    for_each_dispatcher<(I + 1) == sizeof...(Args), (I+1), F, Args...>::run(args, f);
  }
};

template<std::size_t I, typename F, typename ...Args>
struct for_each_dispatcher<true, I, F, Args...>  {
  static void run(std::tuple<Args...>& args, const F& f) {}  // NOLINT(*)
};
}  // namespace detail

template<typename F, typename ...Args>
inline void for_each(std::tuple<Args...>& args, const F& f) {  // NOLINT(*)
  detail::for_each_dispatcher<sizeof...(Args) == 0, 0, F, Args...>::run(args, f);
}

/* \brief argument settter to PackedFunc */
class TVMArgsSetter {
 public:
  TVMArgsSetter(TVMValue* values, int* type_codes)
      : values_(values), type_codes_(type_codes) {}
  // setters for POD types
  template<typename T,
           typename = typename std::enable_if<std::is_integral<T>::value>::type>
  void operator()(size_t i, T value) const {
    values_[i].v_int64 = static_cast<int64_t>(value);
    type_codes_[i] = kInt;
  }
  void operator()(size_t i, uint64_t value) const {
    values_[i].v_int64 = static_cast<int64_t>(value);
    CHECK_LE(value,
             static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
    type_codes_[i] = kInt;
  }
  void operator()(size_t i, double value) const {
    values_[i].v_float64 = value;
    type_codes_[i] = kFloat;
  }
  void operator()(size_t i, std::nullptr_t value) const {
    values_[i].v_handle = value;
    type_codes_[i] = kNull;
  }
  void operator()(size_t i, const TVMArgValue& value) const {
    values_[i] = value.value_;
    type_codes_[i] = value.type_code_;
  }
  void operator()(size_t i, void* value) const {
    values_[i].v_handle = value;
    type_codes_[i] = kHandle;
  }
  void operator()(size_t i, TVMArray* value) const {
    values_[i].v_handle = value;
    type_codes_[i] = kArrayHandle;
  }
  void operator()(size_t i, TVMType value) const {
    values_[i].v_type = value;
    type_codes_[i] = kTVMType;
  }
  void operator()(size_t i, const char* value) const {
    values_[i].v_str = value;
    type_codes_[i] = kStr;
  }
  // setters for container type
  // They must be reference(instead of const ref)
  // to make sure they are alive in the tuple(instead of getting converted)
  void operator()(size_t i, std::string& value) const {  // NOLINT(*)
    values_[i].v_str = value.c_str();
    type_codes_[i] = kStr;
  }
  void operator()(size_t i, PackedFunc& value) const {  // NOLINT(*)
    values_[i].v_handle = &value;
    type_codes_[i] = kFuncHandle;
  }
  void operator()(size_t i, Module& value) const {  // NOLINT(*)
    values_[i].v_handle = &value;
    type_codes_[i] = kModuleHandle;
  }
  void operator()(size_t i, TVMRetValue& value) const {  // NOLINT(*)
    if (value.type_code() == kStr) {
      values_[i].v_str = value.ptr<std::string>()->c_str();
      type_codes_[i] = kStr;
    } else {
      values_[i] = value.value_;
      type_codes_[i] = value.type_code();
    }
  }
  // NodeRef related extenstions: in tvm/packed_func_ext.h
  inline void operator()(size_t i, NodeRef& other) const;  // NOLINT(*)
  inline void operator()(size_t i, const Halide::Type& t) const;

 private:
  /*! \brief The values fields */
  TVMValue* values_;
  /*! \brief The type code fields */
  int* type_codes_;
};

class TVMArgsGetter {
 public:
  explicit TVMArgsGetter(TVMArgs args)
      : args_(args) {}

  template<typename T>
  inline void operator()(size_t i, T& target) const {  // NOLINT(*)
    target = args_[i].operator T();
  }
 private:
  TVMArgs args_;
};

template<typename... Args>
inline TVMRetValue PackedFunc::operator()(Args&& ...args) const {
  auto targs = std::make_tuple(std::forward<Args>(args)...);
  const int kNumArgs = sizeof...(Args);
  TVMValue values[kNumArgs];
  int type_codes[kNumArgs];
  for_each(targs, TVMArgsSetter(values, type_codes));
  TVMRetValue rv;
  body_(TVMArgs(values, type_codes, kNumArgs), &rv);
  return rv;
}

}  // namespace runtime
}  // namespace tvm
#endif  // TVM_RUNTIME_PACKED_FUNC_H_