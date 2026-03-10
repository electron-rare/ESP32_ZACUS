// app_modules.h - concrete IAppModule implementations for core apps.
#pragma once

#include <memory>

#include "app/app_runtime_types.h"

namespace app::modules {

std::unique_ptr<IAppModule> createAppModule(const AppDescriptor& descriptor);

}  // namespace app::modules

