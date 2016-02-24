// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_INTERFACE_PTR_SET_H_
#define MOJO_PUBLIC_CPP_BINDINGS_INTERFACE_PTR_SET_H_

#include <utility>
#include <vector>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"

namespace mojo {

template <typename Interface>
class InterfacePtrSet {
 public:
  InterfacePtrSet() {}
  ~InterfacePtrSet() { CloseAll(); }

  void AddInterfacePtr(InterfacePtr<Interface> ptr) {
    auto weak_interface_ptr = new Element(std::move(ptr));
    ptrs_.push_back(weak_interface_ptr->GetWeakPtr());
    ClearNullInterfacePtrs();
  }

  template <typename FunctionType>
  void ForAllPtrs(FunctionType function) {
    for (const auto& it : ptrs_) {
      if (it)
        function(it->get());
    }
    ClearNullInterfacePtrs();
  }

  void CloseAll() {
    for (const auto& it : ptrs_) {
      if (it)
        it->Close();
    }
    ptrs_.clear();
  }

 private:
  class Element {
   public:
    explicit Element(InterfacePtr<Interface> ptr)
        : ptr_(std::move(ptr)), weak_ptr_factory_(this) {
      ptr_.set_connection_error_handler([this]() { delete this; });
    }
    ~Element() {}

    void Close() { ptr_.reset(); }

    Interface* get() { return ptr_.get(); }

    base::WeakPtr<Element> GetWeakPtr() {
      return weak_ptr_factory_.GetWeakPtr();
    }

   private:
    InterfacePtr<Interface> ptr_;
    base::WeakPtrFactory<Element> weak_ptr_factory_;

    DISALLOW_COPY_AND_ASSIGN(Element);
  };

  void ClearNullInterfacePtrs() {
    ptrs_.erase(std::remove_if(ptrs_.begin(), ptrs_.end(),
                               [](const base::WeakPtr<Element>& p) {
                                 return p.get() == nullptr;
                               }),
                ptrs_.end());
  }

  std::vector<base::WeakPtr<Element>> ptrs_;
};

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_INTERFACE_PTR_SET_H_
