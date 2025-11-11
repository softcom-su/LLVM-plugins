#ifndef LLVM_TUTOR_DISPATCHER_FACTORY_H
#define LLVM_TUTOR_DISPATCHER_FACTORY_H

#include "Dispatcher.h"
#include <unordered_map>
#include <functional>
#include <memory>

using DispatcherFactory = std::function<std::unique_ptr<Dispatcher>()>;

class DispatcherRegistry {
public:
  static DispatcherRegistry &instance() {
    static DispatcherRegistry r;
    return r;
  }

  void registerDispatcher(DispatcherType type, DispatcherFactory factory) {
    factories[type] = std::move(factory);
  }

  std::unique_ptr<Dispatcher> create(DispatcherType type) {
    auto it = factories.find(type);
    if (it == factories.end())
      llvm::report_fatal_error("Unsupported dispatcher type");
    return it->second();
  }

private:
  std::unordered_map<DispatcherType, DispatcherFactory> factories;
};

#endif